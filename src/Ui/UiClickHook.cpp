#include "Ui/UiClickHook.hpp"

#include "Engine/Net/ProtoReflect.hpp"
#include "Engine/Net/ServerSideClients.hpp"
#include "Ui/ClickMessage.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <cstdint>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

// Presses arrive as CSVCMsg_UserMessage of type CS_UM_CustomHudClicked, which the SDK does not define.
static constexpr std::string_view UserMessageName = "CSVCMsg_UserMessage";
static constexpr int32_t CustomHudClickType = 390;

/** Press fields resolved once per process from the engine's descriptor pool. */
struct PressFields
{
    const ProtoFieldDescriptor* Type = nullptr;
    const ProtoFieldDescriptor* Data = nullptr;

    explicit operator bool() const { return Type && Data; }
};

static const PressFields& PressFieldsOf(const ProtoMessage& proto)
{
    static const PressFields fields = [&proto] {
        const PressFields resolved{.Type = ProtoField(proto, "msg_type"), .Data = ProtoField(proto, "msg_data")};
        if (!resolved)
            Log::Warn("UiClickHook: {} has no 'msg_type'/'msg_data' field; ignoring presses.", UserMessageName);
        return resolved;
    }();
    return fields;
}

/** Layout-handle bits sent by the client: 14 for the index and 10 for the serial. */
static constexpr uint32_t HandleIndexMask = (1u << 14) - 1;
static constexpr uint32_t HandleSerialMask = (1u << 10) - 1;

/** The live `custom_hud_layout` matching a 24-bit client handle; a stale or forged one matches none. */
static EntityRef FindLayout(EntitySystem& entities, uint32_t networked)
{
    std::optional<Entity> cursor(entities.FindByClassName({}, "custom_hud_layout"));
    while (*cursor)
    {
        const EntityRef ref = cursor->Ref();
        if ((ref.Handle & HandleIndexMask) == (networked & HandleIndexMask) &&
            ((ref.Handle >> 15) & HandleSerialMask) == ((networked >> 14) & HandleSerialMask))
            return ref;

        cursor.emplace(entities.FindByClassName(*cursor, "custom_hud_layout"));
    }
    return {};
}

UiClickHook::UiClickHook(Interfaces& interfaces, const Bindings& bindings, EntitySystem& entities,
                         Scheduler& scheduler, Event<const UiClick&>& clicked)
    : _interfaces(interfaces), _bindings(bindings), _entities(entities), _scheduler(scheduler), _clicked(clicked)
{}

UiClickHook::~UiClickHook()
{
    // A remaining hook outlives Runtime and may point into an unloaded module.
    if (_hook)
        Log::Error("UiClickHook: a click subscription outlived the hook; a click handler may dangle.");
}

bool UiClickHook::Install()
{
    if (!_bindings.FilterMessage || !_bindings.ClientMessageFilter)
    {
        Log::Warn("UiClickHook: FilterMessage or its client offset did not bind; button presses will not arrive.");
        return false;
    }
    if (auto* message = _interfaces.NetworkMessages
                            ? _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(UserMessageName).c_str())
                            : nullptr)
        _messageId = message->GetNetMessageInfo()->m_MessageId;
    if (_messageId < 0)
    {
        Log::Warn("UiClickHook: the engine does not know {}; button presses will not arrive.", UserMessageName);
        return false;
    }

    auto hook = HookFunction("Custom HUD clicks", _bindings.FilterMessage,
                             [this](EngineMessageFilter& filter, const CNetMessage* message, void*) {
                                 // This observer never changes the verdict.
                                 QueuePress(message, filter);
                             });
    if (!hook)
    {
        Log::Warn("UiClickHook: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _onFrame = _scheduler.EveryFrame([this] { RaisePresses(); });
    Log::Info("UiClickHook: listening for user message {}, click type {}.", _messageId, CustomHudClickType);
    return true;
}

void UiClickHook::Remove()
{
    _hook.Reset();
    _onFrame.Reset();
    _queued.clear();
    _messageId = -1;
}

void UiClickHook::QueuePress(const CNetMessage* message, const EngineMessageFilter& filter)
{
    // Filter all inbound messages by id before parsing.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const PressFields& fields = PressFieldsOf(*proto);
    if (!fields)
        return;

    // The type narrows the shared wrapper to a HUD press.
    const auto* reflection = proto->GetReflection();
    if (reflection->GetInt32(*proto, fields.Type) != CustomHudClickType)
        return;

    // FilterMessage's `this` is a base inside the client.
    const auto* client = reinterpret_cast<const uint8_t*>(&filter) - _bindings.ClientMessageFilter.Value();
    const int slot = SlotOfClient(_bindings, client);
    if (!IsValidSlot(slot))
        return;

    auto payload = ParseClickMessage(reflection->GetString(*proto, fields.Data));
    if (!payload)
    {
        Log::Warn("UiClickHook: a press from slot {} did not parse ({}).", slot, payload.error().Detail);
        return;
    }

    // Reject embedded NULs in client-controlled text before formatting it.
    if (payload->ButtonId.find('\0') != std::string::npos)
        return;

    _queued.push_back({.Slot = slot, .LayoutHandle = payload->LayoutHandle, .ButtonId = std::move(payload->ButtonId)});
}

void UiClickHook::RaisePresses()
{
    // Swap before dispatch so a handler can remove the hook and clear the queue safely.
    std::vector<QueuedPress> presses;
    presses.swap(_queued);

    for (QueuedPress& press : presses)
    {
        EntityRef layout = FindLayout(_entities, press.LayoutHandle);
        if (!layout)
            continue;  // stale press from a layout that has since been removed

        _clicked.Raise(UiClick{.Slot = press.Slot, .LayoutEntity = layout, .ButtonId = std::move(press.ButtonId)});
    }
}

}  // namespace VoltMod
