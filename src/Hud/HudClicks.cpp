#include "Engine/ServerSideClients.hpp"
#include "Engine/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Hud/HudClicks.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <google/protobuf/message.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <utility>

namespace VoltMod
{

// Hooks CServerSideClient::FilterMessage, the inbound client-message filter. The channel is
// unused, so it stays opaque.
VOLTMOD_VHOOK2(VoltMod_FilterMessage, bool, const CNetMessage*, void*);

/** The user message carrying a custom HUD button press. */
static constexpr const char* kClickMessage = "CCSUsrMsg_CustomHudClicked";

using ProtoMessage = google::protobuf::Message;

/**
 * Read @p name out of @p message by reflection.
 *
 * The CS-specific user messages are declared in the SDK's cstrike15_usermessages.proto but are
 * not generated into headers, and consumer builds deliberately do not run protoc - the same
 * situation Vote.cpp is in. The engine has registered the descriptors, so the two fields are read
 * by name: a renamed field degrades to an empty click rather than miscompiling.
 */
static const google::protobuf::FieldDescriptor* ProtoField(const ProtoMessage& message, const char* name)
{
    const auto* descriptor = message.GetDescriptor();
    return descriptor ? descriptor->FindFieldByName(name) : nullptr;
}

HudClicks::HudClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots)
    : Clicked({.OnFirst = [this] { return Acquire(); }, .OnLast = [this] { ReleaseRef(); }}),
      _interfaces(interfaces),
      _bindings(bindings),
      _slots(slots)
{}

HudClicks::~HudClicks()
{
    // Never leave a hook pointing into an unloaded module.
    if (_refs != 0)
        Log::Error("HudClicks: {} subscription(s) outlived the hook; a click handler may dangle.", _refs);
}

bool HudClicks::Acquire()
{
    if (_refs == 0)
    {
        if (!_bindings.FilterMessage)
        {
            Log::Warn("HudClicks: FilterMessage did not bind; button presses will not arrive.");
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

void HudClicks::ReleaseRef()
{
    if (_refs > 0 && --_refs == 0)
    {
        _connectListener.Reset();
        _hook.Reset();
        _messageId = -1;
        _baseOffset = 0;
    }
}

bool HudClicks::Install()
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
        Log::Warn("HudClicks: FilterMessage is in none of CServerSideClient's vtables; not hooking.");
        _connectListener.Reset();  // a retry cannot change this
        return false;
    }

    if (_interfaces.NetworkMessages)
    {
        if (INetworkMessageInternal* message = _interfaces.NetworkMessages->FindNetworkMessage(kClickMessage))
            _messageId = message->GetNetMessageInfo()->m_MessageId;
    }
    if (_messageId < 0)
    {
        Log::Warn("HudClicks: the engine does not know {}; not hooking.", kClickMessage);
        _connectListener.Reset();
        return false;
    }

    // Built here rather than taken from Bindings: this binding's table and index come from the
    // instance search above, not from a gamedata index.
    using FilterSig = bool(const CNetMessage*, void*);
    const VHookBinding<FilterSig> binding{.Method = VFn<FilterSig>(slot->Index),
                                          .Table = VTableRef("CServerSideClient", slot->Table)};

    auto hook = VtableHook::OnVTable<VoltMod_FilterMessageHook>("Custom HUD clicks", binding, this,
                                                                &HudClicks::Hook_FilterMessage, nullptr);
    if (!hook)
    {
        Log::Warn("HudClicks: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _baseOffset = slot->BaseOffset;
    Log::Info("HudClicks: hooked FilterMessage at index {} (+{} from the client), message id {}.", slot->Index,
              _baseOffset, _messageId);
    return true;
}

bool HudClicks::Hook_FilterMessage(const CNetMessage* message, void*)
{
    // Every inbound message from every client lands here, so the id check comes first and costs
    // two loads; the reflection below only runs for an actual click.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        RETURN_META_VALUE(MRES_IGNORED, true);

    // A DVP hook on a secondary vtable is called with that subobject, not the client.
    auto* self = static_cast<uint8_t*>(META_IFACEPTR(void));
    const void* client = self ? self - _baseOffset : nullptr;
    const int slot = SlotOfServerSideClient(_bindings, client);
    if (!IsValidSlot(slot))
        RETURN_META_VALUE(MRES_IGNORED, true);

    const ProtoMessage& proto = *message->ToPB<ProtoMessage>();
    const auto* layoutField = ProtoField(proto, "custom_hud_layout");
    const auto* buttonField = ProtoField(proto, "button_id");
    if (!layoutField || !buttonField)
    {
        Log::Warn("HudClicks: {} has no 'custom_hud_layout'/'button_id' field; ignoring the press.", kClickMessage);
        RETURN_META_VALUE(MRES_IGNORED, true);
    }

    const auto* reflection = proto.GetReflection();
    HudClick click{.Slot = slot,
                   .Layout = EntityRef{reflection->GetUInt32(proto, layoutField)},
                   .ButtonId = reflection->GetString(proto, buttonField)};

    // The button id is client-controlled text. Handlers compare it against ids they authored, so
    // it is passed through as-is, but an embedded NUL would truncate it anywhere it is formatted.
    if (click.ButtonId.find('\0') != std::string::npos)
        RETURN_META_VALUE(MRES_IGNORED, true);

    Clicked.Raise(click);
    RETURN_META_VALUE(MRES_IGNORED, true);
}

}  // namespace VoltMod
