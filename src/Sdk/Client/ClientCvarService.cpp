#include "Sdk/Internal/VtableLookup.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/MetamodGlobals.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/TimeUtils.hpp>
#include <VoltMod/Sdk/Client/ClientCvarService.hpp>
#include <VoltMod/Sdk/Client/Detail/ClientCvarPending.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <cstdint>
#include <cstring>
#include <engine/igameeventsystem.h>
#include <inetchannel.h>
#include <netmessages.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>

PLUGIN_GLOBALVARS();

namespace VoltMod::Sdk
{
using namespace VoltMod::Core;

// CServerSideClient::ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>&); the
// vtable index is reconfigured from gamedata at Initialize time. Bound to the class vtable
// (DVP hook), so it fires for every connected client without needing per-instance bindings.
SH_DECL_MANUALHOOK1(VoltMod_ProcessRespondCvarValue, 0, 0, 0, bool, const CNetMessagePB<CCLCMsg_RespondCvarValue>&);

namespace
{

// The engine module owning CServerSideClient and IVEngineServer2.
constexpr const char* EngineModule = "engine2";
constexpr const char* ServerSideClientClass = "CServerSideClient";

}  // namespace

class ClientCvarService::Impl
{
public:
    Impl(GameInterfaces& interfaces, GameData& gameData) : _interfaces(interfaces), _gameData(gameData) {}
    ~Impl() { Shutdown(); }

    bool Initialize();
    void Shutdown();

    bool Available() const { return _hookId != 0; }
    bool Query(int slot, const std::string& cvarName, QueryCallback callback);

    Detail::ClientCvarPendingTable& Pending() { return _pending; }

private:
    bool Hook_ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg);

    /** Route a validated response to the callback that asked for it. */
    void Deliver(int slot, const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg);

    /** Post CSVCMsg_GetCvarValue to @p slot alone. False for bots and empty slots. */
    bool Send(int slot, const std::string& cvarName, int cookie);

    GameInterfaces& _interfaces;
    GameData& _gameData;
    Detail::ClientCvarPendingTable _pending;
    INetworkMessageInternal* _getCvarValue = nullptr;
    int _slotOffset = -1;
    int _hookId = 0;
};

bool ClientCvarService::Impl::Initialize()
{
    if (_hookId != 0)
        return true;

    const auto& interfaces = _interfaces;
    if (!interfaces.Engine || !interfaces.NetworkMessages || !interfaces.GameEventSystem)
    {
        Log::Warn("ClientCvarService: engine interfaces unavailable.");
        return false;
    }

    const int vtableIndex = _gameData.GetVtableIndex("ProcessRespondCvarValue");
    if (vtableIndex < 0)
        return false;

    const int slotOffset = _gameData.GetByteOffset("ServerSideClientSlot", MaxByteOffset, alignof(int));
    if (slotOffset < 0)
        return false;

    INetworkMessageInternal* getCvarValue =
        interfaces.NetworkMessages->FindNetworkMessagePartial("CSVCMsg_GetCvarValue");
    if (!getCvarValue)
    {
        Log::Warn("ClientCvarService: the engine does not provide CSVCMsg_GetCvarValue.");
        return false;
    }

    void* vtable = FindVirtualTable(EngineModule, ServerSideClientClass);
    if (!vtable)
        return false;  // FindVirtualTable already logged which step failed

    SH_MANUALHOOK_RECONFIGURE(VoltMod_ProcessRespondCvarValue, vtableIndex, 0, 0);
    _hookId = SH_ADD_MANUALDVPHOOK(VoltMod_ProcessRespondCvarValue, vtable,
                                   SH_MEMBER(this, &Impl::Hook_ProcessRespondCvarValue), true);
    if (_hookId == 0)
    {
        Log::Warn("ClientCvarService: could not hook the client convar response handler.");
        return false;
    }

    _getCvarValue = getCvarValue;
    _slotOffset = slotOffset;
    Log::Info("Client convar queries enabled (vtable index {}, slot offset {}).", vtableIndex, slotOffset);
    return true;
}

void ClientCvarService::Impl::Shutdown()
{
    if (_hookId != 0)
    {
        SH_REMOVE_HOOK_ID(_hookId);
        _hookId = 0;
    }
    _pending.ClearAll();
    _getCvarValue = nullptr;
    _slotOffset = -1;
}

bool ClientCvarService::Impl::Query(int slot, const std::string& cvarName, QueryCallback callback)
{
    if (!Available() || !Core::IsValidSlot(slot) || cvarName.empty() || !callback)
        return false;

    const double now = TimeUtils::MonotonicSeconds();
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

bool ClientCvarService::Impl::Send(int slot, const std::string& cvarName, int cookie)
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

bool ClientCvarService::Impl::Hook_ProcessRespondCvarValue(const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg)
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

void ClientCvarService::Impl::Deliver(int slot, const CNetMessagePB<CCLCMsg_RespondCvarValue>& msg)
{
    if (!Core::IsValidSlot(slot) || !msg.has_cookie() || !msg.has_status_code() || !msg.has_name())
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

std::string_view ToString(ClientCvarStatus status)
{
    switch (status)
    {
    case ClientCvarStatus::ValueIntact:
        return "value_intact";
    case ClientCvarStatus::CvarNotFound:
        return "cvar_not_found";
    case ClientCvarStatus::NotACvar:
        return "not_a_cvar";
    case ClientCvarStatus::CvarProtected:
        return "cvar_protected";
    }
    return "unknown";
}

ClientCvarService::ClientCvarService(GameInterfaces& interfaces, GameData& gameData, Core::SlotEvents& slots)
    : _impl(std::make_unique<Impl>(interfaces, gameData))
{
    // SlotEvents fires when a slot is filled as well as emptied; a fresh occupant has nothing
    // pending, so dropping on both edges covers "left" without a dedicated event.
    _slotListener = slots.Listen([this](int slot) { _impl->Pending().Clear(slot); });
}

ClientCvarService::~ClientCvarService() = default;

bool ClientCvarService::Initialize()
{
    return _impl->Initialize();
}

void ClientCvarService::Shutdown()
{
    _impl->Shutdown();
}

bool ClientCvarService::Available() const
{
    return _impl->Available();
}

bool ClientCvarService::Query(int slot, const std::string& cvarName, QueryCallback callback)
{
    return _impl->Query(slot, cvarName, std::move(callback));
}

size_t ClientCvarService::PendingCount(int slot) const
{
    return _impl->Pending().Count(slot);
}

void ClientCvarService::OnClientFullyConnect(int slot)
{
    // Whoever held this slot before is gone, and anything still pending is addressed to them.
    _impl->Pending().Clear(slot);
}

void ClientCvarService::OnServerStartup()
{
    _impl->Pending().ClearAll();
}

}  // namespace VoltMod::Sdk
