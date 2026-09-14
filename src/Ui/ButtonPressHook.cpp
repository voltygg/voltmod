#include "Ui/ButtonPressHook.hpp"

#include "Engine/Net/ServerSideClients.hpp"
#include "Ui/ButtonPressMessage.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <optional>
#include <string_view>
#include <utility>

namespace VoltMod
{

// Presses arrive as CSVCMsg_UserMessage of type CS_UM_CustomHudClicked, which the SDK does not define.
static constexpr std::string_view UserMessageName = "CSVCMsg_UserMessage";
static constexpr int32_t CustomHudClickType = 390;

/** Layout-handle bits the client sends: 14 for the index and 10 for the serial. */
static constexpr uint32_t HandleIndexMask = (1u << 14) - 1;
static constexpr uint32_t HandleSerialMask = (1u << 10) - 1;

ButtonPressHook::ButtonPressHook(Interfaces& interfaces, const Bindings& bindings, EntitySystem& entities,
                                 Scheduler& scheduler, Event<const ButtonPress&>& pressed)
    : _interfaces(interfaces), _bindings(bindings), _entities(entities), _scheduler(scheduler), _pressed(pressed)
{}

ButtonPressHook::~ButtonPressHook()
{
    // A remaining hook outlives Runtime and may point into an unloaded module.
    if (_hook)
        Log::Error("ButtonPressHook: a press subscription outlived the hook; a handler may dangle.");
}

bool ButtonPressHook::Install()
{
    if (!_bindings.ClientMessageFilter)
    {
        Log::Warn("ButtonPressHook: the FilterMessage client offset did not bind; button presses will not arrive.");
        return false;
    }
    if (auto* message = _interfaces.NetworkMessages
                            ? _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(UserMessageName).c_str())
                            : nullptr)
        _messageId = message->GetNetMessageInfo()->m_MessageId;
    if (_messageId < 0)
    {
        Log::Warn("ButtonPressHook: the engine does not know {}; button presses will not arrive.", UserMessageName);
        return false;
    }

    auto hook = HookFunction("Custom HUD button presses", _bindings.FilterMessage,
                             [this](EngineMessageFilter& filter, const CNetMessage* message, void*) {
                                 Queue(message, filter);
                             });
    if (!hook)
    {
        Log::Warn("ButtonPressHook: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _onFrame = _scheduler.EveryFrame([this] { RaiseQueued(); });
    Log::Info("ButtonPressHook: listening for user message {}, press type {}.", _messageId, CustomHudClickType);
    return true;
}

void ButtonPressHook::Remove()
{
    _hook.Reset();
    _onFrame.Reset();
    _queued.clear();
    _messageId = -1;
}

const ButtonPressHook::MessageFields& ButtonPressHook::FieldsOf(const ProtoMessage& proto)
{
    static const MessageFields fields = [&proto] {
        const MessageFields resolved{.Type = ProtoField(proto, "msg_type"), .Data = ProtoField(proto, "msg_data")};
        if (!resolved)
            Log::Warn("ButtonPressHook: {} has no 'msg_type'/'msg_data' field; ignoring presses.", UserMessageName);
        return resolved;
    }();
    return fields;
}

void ButtonPressHook::Queue(const CNetMessage* message, const EngineMessageFilter& filter)
{
    // Every inbound message passes here, so filter by id before parsing anything.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const MessageFields& fields = FieldsOf(*proto);
    if (!fields)
        return;

    const auto* reflection = proto->GetReflection();
    if (reflection->GetInt32(*proto, fields.Type) != CustomHudClickType)
        return;

    // The sending connection's slot, never the pawn it watches: a spectator's press stays theirs.
    const int slot = SlotOfClient(_bindings, ClientOfFilter(_bindings, filter));
    if (!IsValidSlot(slot))
        return;

    auto payload = ButtonPressMessage::Parse(reflection->GetString(*proto, fields.Data));
    if (!payload)
    {
        Log::Warn("ButtonPressHook: a press from slot {} did not parse ({}).", slot, payload.error().Detail);
        return;
    }

    // Client-controlled text: refuse embedded NULs before anything formats it.
    if (payload->ButtonId.find('\0') != std::string::npos)
        return;

    _queued.push_back({.Slot = slot, .LayoutHandle = payload->LayoutHandle, .ButtonId = std::move(payload->ButtonId)});
}

void ButtonPressHook::RaiseQueued()
{
    // Swapped out first so a handler may remove the hook and clear the queue.
    std::vector<QueuedPress> presses;
    presses.swap(_queued);

    for (QueuedPress& press : presses)
    {
        EntityRef layout = FindLayout(press.LayoutHandle);
        if (!layout)
            continue;

        _pressed.Raise(ButtonPress{.Slot = press.Slot, .Layout = layout, .ButtonId = std::move(press.ButtonId)});
    }
}

EntityRef ButtonPressHook::FindLayout(uint32_t networkedHandle) const
{
    std::optional<Entity> cursor(_entities.FindByClassName({}, "custom_hud_layout"));
    while (*cursor)
    {
        const EntityRef ref = cursor->Ref();
        if ((ref.Handle & HandleIndexMask) == (networkedHandle & HandleIndexMask) &&
            ((ref.Handle >> 15) & HandleSerialMask) == ((networkedHandle >> 14) & HandleSerialMask))
            return ref;

        cursor.emplace(_entities.FindByClassName(*cursor, "custom_hud_layout"));
    }
    return {};
}

}  // namespace VoltMod
