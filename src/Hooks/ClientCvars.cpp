#include "Hooks/ClientCvarPending.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Hooks/ClientCvars.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <cstring>
#include <engine/igameeventsystem.h>
#include <inetchannel.h>
#include <netmessages.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <utility>

namespace VoltMod
{

// CServerSideClient::ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>&).
// Bound to the class vtable (DVP hook), so it fires for every connected client without needing
// per-instance bindings.
VOLTMOD_VHOOK1(VoltMod_ProcessRespondCvarValue, bool, const CNetMessagePB<CCLCMsg_RespondCvarValue>&);

class ClientCvars::Impl
{
public:
    Impl(Interfaces& interfaces, const Bindings& bindings) : _interfaces(interfaces), _bindings(bindings) {}
    ~Impl() { Shutdown(); }

    Status Initialize();
    void Shutdown();

    /** Internal readiness: the hook is what makes a query answerable at all. */
    bool Installed() const { return static_cast<bool>(_hook); }
    bool Query(int slot, const std::string& cvarName, QueryCallback callback);

    ClientCvarPendingTable& Pending() { return _pending; }

private:
    bool Hook_ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg);

    /** Route a validated response to the callback that asked for it. */
    void Deliver(int slot, const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg);

    /** Post CSVCMsg_GetCvarValue to @p slot alone. False for bots and empty slots. */
    bool Send(int slot, const std::string& cvarName, int cookie);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    ClientCvarPendingTable _pending;
    INetworkMessageInternal* _getCvarValue = nullptr;
    int _slotOffset = -1;
    VtableHook _hook;
};

Status ClientCvars::Impl::Initialize()
{
    if (Installed())
        return {};

    const auto& interfaces = _interfaces;
    if (!interfaces.Engine || !interfaces.NetworkMessages || !interfaces.GameEventSystem)
        return std::unexpected(Error::NotReady("engine interfaces unavailable"));

    if (!_bindings.ServerSideClientSlot)
        return std::unexpected(Error::Unsupported("the ServerSideClientSlot offset did not bind"));

    INetworkMessageInternal* getCvarValue =
        interfaces.NetworkMessages->FindNetworkMessagePartial("CSVCMsg_GetCvarValue");
    if (!getCvarValue)
        return std::unexpected(Error::Engine("the engine does not provide CSVCMsg_GetCvarValue"));

    auto hook = VtableHook::OnVTable<VoltMod_ProcessRespondCvarValueHook>(
        "Client convar response", _bindings.ServerSideClient, _bindings.ProcessRespondCvarValue.Index(), this, nullptr,
        &Impl::Hook_ProcessRespondCvarValue);
    if (!hook)
        return std::unexpected(hook.error());

    _hook = std::move(*hook);
    _getCvarValue = getCvarValue;
    _slotOffset = _bindings.ServerSideClientSlot.Value();
    Log::Info("Client convar queries enabled (slot offset {}).", _slotOffset);
    return {};
}

void ClientCvars::Impl::Shutdown()
{
    _hook.Reset();
    _pending.ClearAll();
    _getCvarValue = nullptr;
    _slotOffset = -1;
}

bool ClientCvars::Impl::Query(int slot, const std::string& cvarName, QueryCallback callback)
{
    if (!Installed() || !IsValidSlot(slot) || cvarName.empty() || !callback)
        return false;

    const double now = Time::MonotonicSeconds();
    _pending.Prune(slot, now);

    // An answer is an answer whoever asked for it, so re-point the outstanding request rather than
    // making the client encode the same convar twice.
    if (_pending.Retarget(slot, cvarName, callback))
        return true;

    if (_pending.Full(slot))
        return false;

    const int cookie = _pending.NextCookie(slot);
    if (cookie < 0 || !Send(slot, cvarName, cookie))
        return false;

    _pending.Add(slot, cookie, cvarName, std::move(callback), now);
    return true;
}

bool ClientCvars::Impl::Send(int slot, const std::string& cvarName, int cookie)
{
    const auto& interfaces = _interfaces;

    // Bots and empty slots have no net channel, so posting would be a silent no-op leaving a
    // pending entry to time out.
    if (!interfaces.Engine->GetPlayerNetInfo(CPlayerSlot(slot)))
        return false;

    CNetMessage* message = _getCvarValue->AllocateMessage();
    if (!message)
        return false;

    auto* request = message->ToPB<CSVCMsg_GetCvarValue>();
    if (!request)
    {
        interfaces.NetworkMessages->DeallocateNetMessageAbstract(_getCvarValue, message);
        return false;
    }

    request->set_cookie(cookie);
    request->set_cvar_name(cvarName);

    const uint64 recipients = 1ull << slot;
    interfaces.GameEventSystem->PostEventAbstract(-1, false, slot + 1, &recipients, _getCvarValue, request, 0,
                                                  BUF_RELIABLE);
    interfaces.NetworkMessages->DeallocateNetMessageAbstract(_getCvarValue, message);
    return true;
}

bool ClientCvars::Impl::Hook_ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg)
{
    // The responding client is the hooked object. Its slot lives at a gamedata byte offset because
    // the SDK does not declare CServerSideClient's layout.
    if (void* client = META_IFACEPTR(void); client && _slotOffset >= 0)
    {
        int slot = -1;
        std::memcpy(&slot, static_cast<const uint8_t*>(client) + _slotOffset, sizeof(slot));
        Deliver(slot, msg);
    }

    // Never alter the engine's own handling of the response.
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void ClientCvars::Impl::Deliver(int slot, const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg)
{
    if (!IsValidSlot(slot) || !msg.has_cookie() || !msg.has_status_code() || !msg.has_name())
        return;

    // Everything below is client-controlled, so each field is checked before it is trusted: an
    // unknown status code, a name that does not match what this cookie asked for, or a value with
    // embedded NULs (which truncate anywhere it is treated as a C string).
    const int status = msg.status_code();
    if (status < static_cast<int>(ClientCvarStatus::ValueIntact) ||
        status > static_cast<int>(ClientCvarStatus::CvarProtected))
        return;

    std::string_view value;
    if (status == static_cast<int>(ClientCvarStatus::ValueIntact))
    {
        if (!msg.has_value() || msg.value().find('\0') != std::string::npos)
            return;
        value = msg.value();
    }

    // Taking the entry first means a callback is free to issue a fresh query for the same convar.
    auto query = _pending.Take(slot, msg.cookie(), msg.name());
    if (query && query->Callback)
        query->Callback(slot, static_cast<ClientCvarStatus>(status), msg.name(), value);
}

ClientCvars::ClientCvars(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots)
    : _impl(std::make_unique<Impl>(interfaces, bindings))
{
    // SlotEvents fires when a slot is filled as well as emptied; a fresh occupant has nothing
    // pending, so dropping on both edges covers "left" without a dedicated event.
    _slotListener = slots.Changed += [this](int slot) { _impl->Pending().Clear(slot); };
}

ClientCvars::~ClientCvars() = default;

Status ClientCvars::Initialize()
{
    return _impl->Initialize();
}

void ClientCvars::Shutdown()
{
    _impl->Shutdown();
}

bool ClientCvars::Query(int slot, const std::string& cvarName, QueryCallback callback)
{
    return _impl->Query(slot, cvarName, std::move(callback));
}

size_t ClientCvars::PendingCount(int slot) const
{
    return _impl->Pending().Count(slot);
}

void ClientCvars::OnClientFullyConnect(int slot)
{
    // Whoever held this slot before is gone, and anything still pending is addressed to them.
    _impl->Pending().Clear(slot);
}

void ClientCvars::OnServerStartup()
{
    _impl->Pending().ClearAll();
}

}  // namespace VoltMod
