#include "Engine/ProtoReflect.hpp"
#include "Engine/ServerSideClients.hpp"
#include "Engine/VtableLookup.hpp"
#include "Ui/ClickPayload.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Ui/UiClicks.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

// Hooks CServerSideClient::FilterMessage, the inbound client-message filter. The channel is
// unused, so it stays opaque.
VOLTMOD_VHOOK2(VoltMod_FilterMessage, bool, const CNetMessage*, void*);

/**
 * The envelope every user message arrives in.
 *
 * A press is not its own registered message: it rides inside this one's `msg_data`, tagged with a
 * `msg_type` that is named nowhere - hence @ref Bindings::CustomHudClicked. Looked up by partial
 * name because a registered message carries its id in its name (see PostUserMessage).
 */
static constexpr std::string_view kEnvelope = "CSVCMsg_UserMessage";

/** The two envelope fields a press is read out of. */
struct EnvelopeFields
{
    const ProtoFieldDescriptor* Type = nullptr;
    const ProtoFieldDescriptor* Data = nullptr;

    explicit operator bool() const { return Type && Data; }
};

/**
 * Resolve the envelope's fields, once per process.
 *
 * Process-wide rather than per-load for the same reason the schema field cache is: a field name on
 * a registered message type resolves to the same descriptor for every plugin, Runtime and map, and
 * the descriptor pool belongs to the engine rather than to any load cycle.
 */
static const EnvelopeFields& EnvelopeFieldsOf(const ProtoMessage& proto)
{
    static const EnvelopeFields fields = [&proto] {
        const EnvelopeFields resolved{.Type = ProtoField(proto, "msg_type"), .Data = ProtoField(proto, "msg_data")};
        if (!resolved)
            Log::Warn("UiClicks: {} has no 'msg_type'/'msg_data' field; ignoring presses.", kEnvelope);
        return resolved;
    }();
    return fields;
}

UiClicks::UiClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots)
    : Clicked({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      _interfaces(interfaces),
      _bindings(bindings),
      _slots(slots)
{}

UiClicks::~UiClicks()
{
    // Never leave a hook pointing into an unloaded module.
    if (_refs != 0)
        Log::Error("UiClicks: {} subscription(s) outlived the hook; a click handler may dangle.", _refs);
}

bool UiClicks::Acquire()
{
    if (_refs == 0)
    {
        if (!_bindings.FilterMessage)
        {
            Log::Warn("UiClicks: FilterMessage did not bind; button presses will not arrive.");
            return false;
        }

        // Nobody connected yet is the ordinary case at load: keep the subscription and bind on the
        // first connect instead of refusing it.
        if (!Install())
        {
            _connectListener = _slots.Changed += [this](int) {
                if (!_hook && Install())
                    _connectListener.Reset();
            };
        }
    }
    ++_refs;
    return true;
}

void UiClicks::ReleaseRef()
{
    if (_refs > 0 && --_refs == 0)
    {
        _connectListener.Reset();
        _hook.Reset();
        _messageId = -1;
        _baseOffset = 0;
    }
}

bool UiClicks::Install()
{
    void* client = AnyServerSideClient(_interfaces, _bindings);
    if (!client)
        return false;

    // FilterMessage comes from CServerSideClientBase's third base, so it is in a secondary vtable
    // that FindVirtualTable cannot return. Search the client's tables for the address the
    // signature resolved to: the slot either holds that function or it is not the slot, so unlike
    // an index from gamedata this cannot be off by one.
    const auto slot = FindVTableSlot(client, _bindings.FilterMessage.Ptr());
    if (!slot)
    {
        Log::Warn("UiClicks: FilterMessage is in none of CServerSideClient's vtables; not hooking.");
        _connectListener.Reset();  // a retry cannot change this
        return false;
    }

    if (!_bindings.CustomHudClicked)
    {
        Log::Warn("UiClicks: no custom HUD click message id in gamedata; not hooking.");
        _connectListener.Reset();  // a retry cannot change this
        return false;
    }

    if (_interfaces.NetworkMessages)
    {
        if (auto* message = _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(kEnvelope).c_str()))
            _messageId = message->GetNetMessageInfo()->m_MessageId;
    }
    if (_messageId < 0)
    {
        Log::Warn("UiClicks: the engine does not know {}; not hooking.", kEnvelope);
        _connectListener.Reset();
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
    Log::Info("UiClicks: hooked FilterMessage at index {} (+{} from the client), envelope id {}, click type {}.",
              slot->Index, _baseOffset, _messageId, _bindings.CustomHudClicked.Value());
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
    // Every inbound message from every client lands here, so the id check comes first and costs
    // two loads; the reflection below only runs for an actual click.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const EnvelopeFields& fields = EnvelopeFieldsOf(*proto);
    if (!fields)
        return;

    // Every user message shares this envelope, so the type is what narrows it to a press.
    const auto* reflection = proto->GetReflection();
    if (!_bindings.CustomHudClicked.Is(reflection->GetInt32(*proto, fields.Type)))
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

    // The button id is client-controlled text. Handlers compare it against ids they authored, so
    // it is passed through as-is, but an embedded NUL would truncate it anywhere it is formatted.
    if (payload->Button.find('\0') != std::string::npos)
        return;

    Clicked.Raise(
        UiClick{.Slot = slot, .Layout = UiLayoutRef{payload->Layout}, .ButtonId = std::move(payload->Button)});
}

}  // namespace VoltMod
