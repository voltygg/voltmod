#include "Engine/Net/ServerSideClients.hpp"
#include "Hooks/PendingConVarQueries.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Hooks/ClientConVars.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <cstdint>
#include <engine/igameeventsystem.h>
#include <inetchannel.h>
#include <netmessages.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <utility>

namespace VoltMod
{

ClientConVars::ClientConVars(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots)
    : _interfaces(interfaces), _bindings(bindings), _pending(std::make_unique<PendingConVarQueries>())
{
    // Either slot transition invalidates requests for its previous occupant.
    _slotListener = slots.Changed += [this](int slot) { _pending->Clear(slot); };
}

ClientConVars::~ClientConVars()
{
    Shutdown();
}

Status ClientConVars::Initialize()
{
    if (_hook)
        return {};

    if (!_interfaces.Engine || !_interfaces.NetworkMessages || !_interfaces.GameEventSystem)
        return std::unexpected(Error::NotReady("engine interfaces unavailable"));

    if (!_bindings.ServerSideClientSlot)
        return std::unexpected(Error::Unsupported("the ServerSideClientSlot offset did not bind"));

    INetworkMessageInternal* getCvarValue =
        _interfaces.NetworkMessages->FindNetworkMessagePartial("CSVCMsg_GetCvarValue");
    if (!getCvarValue)
        return std::unexpected(Error::Engine("the engine does not provide CSVCMsg_GetCvarValue"));

    auto hook = HookVTable("Client convar response", _bindings.ProcessRespondCvarValue, this, nullptr,
                           &ClientConVars::Hook_ProcessRespondCvarValue);
    if (!hook)
        return std::unexpected(hook.error());

    _hook = std::move(*hook);
    _getCvarValue = getCvarValue;
    Log::Info("Client convar queries enabled (slot offset {}).", _bindings.ServerSideClientSlot.Value());
    return {};
}

void ClientConVars::Shutdown()
{
    _hook.Reset();
    _pending->ClearAll();
    _getCvarValue = nullptr;
}

bool ClientConVars::Query(int slot, const std::string& cvarName, QueryCallback callback)
{
    if (!_hook || !IsValidSlot(slot) || cvarName.empty() || !callback)
        return false;

    const double now = Time::MonotonicSeconds();
    _pending->Prune(slot, now);

    // Share one client request among callbacks for the same convar.
    if (_pending->Retarget(slot, cvarName, callback))
        return true;

    if (_pending->Full(slot))
        return false;

    const int cookie = _pending->NextCookie(slot);
    if (cookie < 0 || !Send(slot, cvarName, cookie))
        return false;

    _pending->Add(slot, cookie, cvarName, std::move(callback), now);
    return true;
}

size_t ClientConVars::PendingCount(int slot) const
{
    return _pending->Count(slot);
}

void ClientConVars::OnClientFullyConnect(int slot)
{
    _pending->Clear(slot);
}

void ClientConVars::OnServerStartup()
{
    _pending->ClearAll();
}

bool ClientConVars::Send(int slot, const std::string& cvarName, int cookie)
{
    // Bots and empty slots have no network channel.
    if (!_interfaces.Engine->GetPlayerNetInfo(CPlayerSlot(slot)))
        return false;

    CNetMessage* message = _getCvarValue->AllocateMessage();
    if (!message)
        return false;

    auto* request = message->ToPB<CSVCMsg_GetCvarValue>();
    if (!request)
    {
        _interfaces.NetworkMessages->DeallocateNetMessageAbstract(_getCvarValue, message);
        return false;
    }

    request->set_cookie(cookie);
    request->set_cvar_name(cvarName);

    const uint64 recipients = 1ull << slot;
    _interfaces.GameEventSystem->PostEventAbstract(-1, false, slot + 1, &recipients, _getCvarValue, request, 0,
                                                   BUF_RELIABLE);
    _interfaces.NetworkMessages->DeallocateNetMessageAbstract(_getCvarValue, message);
    return true;
}

KHook::Return<bool> ClientConVars::Hook_ProcessRespondCvarValue(VtableObject* client, const void* message)
{
    // The SDK omits CServerSideClient's layout, so gamedata supplies the slot offset; -1 comes
    // back when it did not bind.
    const int slot = SlotOfServerSideClient(_bindings, client);
    const auto& msg = *static_cast<const CNetMessagePB<CCLCMsg_RespondCvarValue>*>(message);

    if (!IsValidSlot(slot) || !msg.has_cookie() || !msg.has_status_code() || !msg.has_name())
        return {KHook::Action::Ignore, true};

    // Validate all client-controlled fields before dispatch.
    const int status = msg.status_code();
    if (status < std::to_underlying(ClientConVarStatus::Answered) ||
        status > std::to_underlying(ClientConVarStatus::Protected))
        return {KHook::Action::Ignore, true};

    std::string_view value;
    if (status == std::to_underlying(ClientConVarStatus::Answered))
    {
        if (!msg.has_value() || msg.value().find('\0') != std::string::npos)
            return {KHook::Action::Ignore, true};
        value = msg.value();
    }

    // Remove first so callbacks may query the same convar again.
    auto query = _pending->Take(slot, msg.cookie(), msg.name());
    if (query && query->Callback)
        query->Callback(slot, static_cast<ClientConVarStatus>(status), msg.name(), value);

    return {KHook::Action::Ignore, true};
}

}  // namespace VoltMod
