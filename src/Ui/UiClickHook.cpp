#include "Ui/UiClickHook.hpp"

#include "Engine/Net/ProtoReflect.hpp"
#include "Engine/Net/ServerSideClients.hpp"
#include "Engine/Memory/VtableLookup.hpp"
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

// Presses ride in CSVCMsg_UserMessage::msg_data, tagged by msg_type. The engine registry and SDK
// do not expose CS_UM_CustomHudClicked, so keep its protocol value here.
static constexpr std::string_view kUserMessage = "CSVCMsg_UserMessage";
static constexpr int32_t kCustomHudClicked = 390;

/** Press fields resolved once per process from the engine's descriptor pool. */
struct UserMessageFields
{
    const ProtoFieldDescriptor* Type = nullptr;
    const ProtoFieldDescriptor* Data = nullptr;

    explicit operator bool() const { return Type && Data; }
};

static const UserMessageFields& FieldsOf(const ProtoMessage& proto)
{
    static const UserMessageFields fields = [&proto] {
        const UserMessageFields resolved{.Type = ProtoField(proto, "msg_type"), .Data = ProtoField(proto, "msg_data")};
        if (!resolved)
            Log::Warn("UiClickHook: {} has no 'msg_type'/'msg_data' field; ignoring presses.", kUserMessage);
        return resolved;
    }();
    return fields;
}

/** Layout-handle bits sent by the client: 14 for the index and 10 for the serial. */
static constexpr uint32_t kNetworkIndexMask = (1u << 14) - 1;
static constexpr uint32_t kNetworkSerialMask = (1u << 10) - 1;

/**
 * Find the live `custom_hud_layout` named by a networked handle.
 *
 * The client sends only 24 handle bits, so walk the live layouts instead of trusting the handle.
 * A stale or forged handle then matches nothing.
 */
static EntityRef ResolveLayout(EntitySystem& entities, uint32_t networked)
{
    std::optional<Entity> cursor(entities.FindByClassName({}, "custom_hud_layout"));
    while (*cursor)
    {
        const EntityRef ref = cursor->Ref();
        if ((ref.Handle & kNetworkIndexMask) == (networked & kNetworkIndexMask) &&
            ((ref.Handle >> 15) & kNetworkSerialMask) == ((networked >> 14) & kNetworkSerialMask))
            return ref;

        cursor.emplace(entities.FindByClassName(*cursor, "custom_hud_layout"));
    }
    return {};
}

UiClickHook::UiClickHook(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots, EntitySystem& entities,
                         Scheduler& scheduler, Event<const UiClick&>& clicked)
    : _interfaces(interfaces),
      _bindings(bindings),
      _slots(slots),
      _entities(entities),
      _scheduler(scheduler),
      _clicked(clicked)
{}

UiClickHook::~UiClickHook()
{
    // A remaining hook outlives Runtime and may point into an unloaded module.
    if (_hook)
        Log::Error("UiClickHook: a click subscription outlived the hook; a click handler may dangle.");
}

bool UiClickHook::Install()
{
    if (!_bindings.FilterMessage)
    {
        Log::Warn("UiClickHook: FilterMessage did not bind; button presses will not arrive.");
        return false;
    }
    if (auto* message = _interfaces.NetworkMessages
                            ? _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(kUserMessage).c_str())
                            : nullptr)
        _messageId = message->GetNetMessageInfo()->m_MessageId;
    if (_messageId < 0)
    {
        Log::Warn("UiClickHook: the engine does not know {}; button presses will not arrive.", kUserMessage);
        return false;
    }

    // If no client exists yet, bind when the first client connects.
    if (!HookConnectedClient())
    {
        _connectListener = _slots.Changed += [this](int) {
            if (!_hook && HookConnectedClient())
                _connectListener.Reset();
        };
    }
    return true;
}

void UiClickHook::Remove()
{
    _connectListener.Reset();
    _hook.Reset();
    _onFrame.Reset();
    _queued.clear();
    _messageId = -1;
    _subobjectOffset = 0;
}

bool UiClickHook::HookConnectedClient()
{
    void* client = AnyServerSideClient(_interfaces, _bindings);
    if (!client)
        return false;

    // FilterMessage is in a secondary vtable; find its slot by searching a live client.
    const auto slot = FindVTableSlot(client, _bindings.FilterMessage.Ptr(), OriginalVfnPtr);
    if (!slot)
    {
        Log::Warn("UiClickHook: FilterMessage is in none of CServerSideClient's vtables; not hooking.");
        _connectListener.Reset();  // a retry cannot change this
        return false;
    }

    // The table and index come from the live-client search, not gamedata.
    using FilterSig = bool(const CNetMessage*, void*);
    const VHookBinding<HookedClientChannel, FilterSig> binding{.Method = VFn<FilterSig>(slot->Index),
                                                               .Table = VTableRef("CServerSideClient", slot->Table)};

    auto hook = HookVTable("Custom HUD clicks", binding,
                           [this](HookedClientChannel& channel, const CNetMessage* message, void*) {
                               // This observer never changes the verdict.
                               QueuePress(message, &channel);
                           });
    if (!hook)
    {
        Log::Warn("UiClickHook: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _subobjectOffset = slot->BaseOffset;
    _onFrame = _scheduler.EveryFrame([this] { RaiseQueued(); });
    Log::Info("UiClickHook: hooked FilterMessage at index {} (+{} from the client), user message id {}, click type {}.",
              slot->Index, _subobjectOffset, _messageId, kCustomHudClicked);
    return true;
}

void UiClickHook::QueuePress(const CNetMessage* message, void* self)
{
    // Filter all inbound messages by id before parsing.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const UserMessageFields& fields = FieldsOf(*proto);
    if (!fields)
        return;

    // The type narrows the shared wrapper to a HUD press.
    const auto* reflection = proto->GetReflection();
    if (reflection->GetInt32(*proto, fields.Type) != kCustomHudClicked)
        return;

    // A secondary-vtable hook receives its subobject, not the client object.
    const void* client = self ? static_cast<uint8_t*>(self) - _subobjectOffset : nullptr;
    const int slot = SlotOfServerSideClient(_bindings, client);
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

void UiClickHook::RaiseQueued()
{
    // Swap before dispatch so a handler can remove the hook and clear the queue safely.
    std::vector<QueuedPress> presses;
    presses.swap(_queued);

    for (QueuedPress& press : presses)
    {
        EntityRef layout = ResolveLayout(_entities, press.LayoutHandle);
        if (!layout)
            continue;  // stale press from a layout that has since been removed

        _clicked.Raise(UiClick{.Slot = press.Slot, .LayoutEntity = layout, .ButtonId = std::move(press.ButtonId)});
    }
}

}  // namespace VoltMod
