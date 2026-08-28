#include "Ui/UiClicks.hpp"

#include "Engine/ProtoReflect.hpp"
#include "Engine/ServerSideClients.hpp"
#include "Engine/VtableLookup.hpp"
#include "Ui/ClickPayload.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

// Hooks CServerSideClient::FilterMessage, the inbound client-message filter. The channel is
// unused, so it stays opaque.
VOLTMOD_VHOOK2(VoltMod_FilterMessage, bool, const CNetMessage*, void*);

// A press is not its own registered message: it rides inside CSVCMsg_UserMessage's `msg_data`,
// tagged with the unnamed `msg_type` bound as Bindings::CustomHudClicked.
static constexpr std::string_view kUserMessage = "CSVCMsg_UserMessage";

/** The two CSVCMsg_UserMessage fields a press is read out of, resolved once per process: field
 *  descriptors belong to the engine's pool, not to any load cycle. */
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
            Log::Warn("UiClicks: {} has no 'msg_type'/'msg_data' field; ignoring presses.", kUserMessage);
        return resolved;
    }();
    return fields;
}

/** Bits of the layout handle as the client sends it: 14 of index, low 10 of the serial. */
static constexpr uint32_t kNetworkIndexMask = (1u << 14) - 1;
static constexpr uint32_t kNetworkSerialMask = (1u << 10) - 1;

/**
 * The layout entity a networked handle names, found among the live `custom_hud_layout` entities.
 *
 * The client sends only 24 of the handle's bits, so this walks the layouts - always a handful -
 * instead of trusting the handle: a stale or forged one matches nothing, and whatever matches is
 * guaranteed to actually be a layout.
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

UiClicks::UiClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots, EntitySystem& entities,
                   Scheduler& scheduler, Event<const UiClick&>& clicked)
    : _interfaces(interfaces),
      _bindings(bindings),
      _slots(slots),
      _entities(entities),
      _scheduler(scheduler),
      _clicked(clicked)
{}

UiClicks::~UiClicks()
{
    // Remove() runs when the last subscription drops, so a hook still up here means one outlived
    // the Runtime - and would point into an unloaded module.
    if (_hook)
        Log::Error("UiClicks: a click subscription outlived the hook; a click handler may dangle.");
}

bool UiClicks::Install()
{
    if (!_bindings.FilterMessage)
    {
        Log::Warn("UiClicks: FilterMessage did not bind; button presses will not arrive.");
        return false;
    }
    if (_bindings.CustomHudClicked < 0)
    {
        Log::Warn("UiClicks: no custom HUD click message id in gamedata; button presses will not arrive.");
        return false;
    }
    if (auto* message = _interfaces.NetworkMessages
                            ? _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(kUserMessage).c_str())
                            : nullptr)
        _messageId = message->GetNetMessageInfo()->m_MessageId;
    if (_messageId < 0)
    {
        Log::Warn("UiClicks: the engine does not know {}; button presses will not arrive.", kUserMessage);
        return false;
    }

    // Nobody connected yet is the ordinary case at load: keep the subscription and bind on the
    // first connect instead of refusing it.
    if (!HookClient())
    {
        _connectListener = _slots.Changed += [this](int) {
            if (!_hook && HookClient())
                _connectListener.Reset();
        };
    }
    return true;
}

void UiClicks::Remove()
{
    _connectListener.Reset();
    _hook.Reset();
    _onFrame.Reset();
    _pending.clear();
    _messageId = -1;
    _baseOffset = 0;
}

bool UiClicks::HookClient()
{
    void* client = AnyServerSideClient(_interfaces, _bindings);
    if (!client)
        return false;

    // FilterMessage lives in a secondary vtable, so the slot is found by searching a live client's
    // tables for the signature's address - see FindVTableSlot.
    const auto slot = FindVTableSlot(client, _bindings.FilterMessage.Ptr(), [](void* entry) -> const void* {
        return g_SHPtr ? g_SHPtr->GetOrigVfnPtrEntry(entry) : nullptr;
    });
    if (!slot)
    {
        Log::Warn("UiClicks: FilterMessage is in none of CServerSideClient's vtables; not hooking.");
        _connectListener.Reset();  // a retry cannot change this
        return false;
    }

    // Built here rather than taken from Bindings: this binding's table and index come from the
    // instance search above, not from a gamedata index.
    using FilterSig = bool(const CNetMessage*, void*);
    const VHookBinding<FilterSig> binding{.Method = VFn<FilterSig>(slot->Index),
                                          .Table = VTableRef("CServerSideClient", slot->Table)};

    auto hook = VtableHook::OnVTable<VoltMod_FilterMessageHook>("Custom HUD clicks", binding, this,
                                                                &UiClicks::Hook_FilterMessage, nullptr);
    if (!hook)
    {
        Log::Warn("UiClicks: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _baseOffset = slot->BaseOffset;
    _onFrame = _scheduler.EveryFrame([this] { DeliverPending(); });
    Log::Info("UiClicks: hooked FilterMessage at index {} (+{} from the client), user message id {}, click type {}.",
              slot->Index, _baseOffset, _messageId, _bindings.CustomHudClicked);
    return true;
}

bool UiClicks::Hook_FilterMessage(const CNetMessage* message, void*)
{
    // Reading a press never changes the verdict, so the hook itself is one unconditional
    // MRES_IGNORED and every early-out below is a plain return.
    HandleMessage(message, META_IFACEPTR(void));
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void UiClicks::HandleMessage(const CNetMessage* message, void* self)
{
    // Every inbound message from every client lands here, so the id check comes first.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const UserMessageFields& fields = FieldsOf(*proto);
    if (!fields)
        return;

    // Every user message shares this wrapper, so the type is what narrows it to a press.
    const auto* reflection = proto->GetReflection();
    if (reflection->GetInt32(*proto, fields.Type) != _bindings.CustomHudClicked)
        return;

    // A DVP hook on a secondary vtable is called with that subobject, not the client.
    const void* client = self ? static_cast<uint8_t*>(self) - _baseOffset : nullptr;
    const int slot = SlotOfServerSideClient(_bindings, client);
    if (!IsValidSlot(slot))
        return;

    auto payload = ParseClickPayload(reflection->GetString(*proto, fields.Data));
    if (!payload)
    {
        Log::Warn("UiClicks: a press from slot {} did not parse ({}).", slot, payload.error().Detail);
        return;
    }

    // Client-controlled text: an embedded NUL would truncate it anywhere it is formatted.
    if (payload->Button.find('\0') != std::string::npos)
        return;

    _pending.push_back({.Slot = slot, .Layout = payload->Layout, .Button = std::move(payload->Button)});
}

void UiClicks::DeliverPending()
{
    // Swapped out first: a handler may drop the last subscription, which disarms and clears the
    // queue.
    std::vector<Pending> presses;
    presses.swap(_pending);

    for (Pending& press : presses)
    {
        EntityRef layout = ResolveLayout(_entities, press.Layout);
        if (!layout)
            continue;  // stale press from a layout that has since been removed

        _clicked.Raise(UiClick{.Slot = press.Slot, .Layout = layout, .ButtonId = std::move(press.Button)});
    }
}

}  // namespace VoltMod
