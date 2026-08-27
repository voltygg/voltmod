#include "Engine/ServerSideClients.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SteamId.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Hooks/Addons.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <algorithm>
#include <eiface.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <utility>

namespace VoltMod
{

// Hooks CServerSideClient::SendNetMessage, the outgoing message path. The buffer type is an SDK
// enum passed by value, taken as int here.
VOLTMOD_VHOOK2(VoltMod_SendNetMessage, bool, const CNetMessage*, int);

Addons::Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler)
    : _interfaces(interfaces), _bindings(bindings), _players(players), _scheduler(scheduler)
{}

Addons::~Addons() = default;

void Addons::Require(uint64_t id)
{
    if (id == 0 || std::ranges::contains(_required, id))
        return;

    _required.push_back(id);
    Arm();
}

void Addons::Drop(uint64_t id)
{
    std::erase(_required, id);
    if (_required.empty())
        Disarm();
}

void Addons::Clear()
{
    _required.clear();
    _clients.clear();
    Disarm();
}

void Addons::RequireFor(int64_t steamId, uint64_t id)
{
    if (id == 0 || !SteamId::IsValid(steamId))
        return;

    auto& extra = _clients[steamId].Extra;
    if (std::ranges::contains(extra, id))
        return;

    extra.push_back(id);
    Arm();
}

void Addons::DropFor(int64_t steamId, uint64_t id)
{
    const auto found = _clients.find(steamId);
    if (found != _clients.end())
        std::erase(found->second.Extra, id);
}

void Addons::Arm()
{
    if (_hook)
        return;

    // A listen server's own client already has whatever the host has, and there is no download
    // step to drive.
    if (!_interfaces.Engine || !_interfaces.Engine->IsDedicatedServer())
    {
        Log::Info("Addons: not a dedicated server; clients will not be sent addon downloads.");
        return;
    }

    auto hook = VtableHook::OnVTable<VoltMod_SendNetMessageHook>("Workshop addon delivery", _bindings.SendNetMessage,
                                                                 this, &Addons::Hook_SendNetMessage, nullptr,
                                                                 AnyServerSideClient(_interfaces, _bindings));
    if (!hook)
    {
        Log::Warn("Addons: {}; clients will not be sent addon downloads.", hook.error().Detail);
        return;
    }
    _hook = std::move(*hook);

    // A reconnect is the only signal that a download finished, so the roster drives the credit.
    _connectListener = _players.Connected += [this](Player& player) { OnConnected(player); };
}

void Addons::Disarm()
{
    // Per-client requirements outlive an empty global list, so only stand down when neither has
    // anything left.
    const bool anyExtra = std::ranges::any_of(_clients, [](const auto& e) { return !e.second.Extra.empty(); });
    if (!_required.empty() || anyExtra)
        return;

    _connectListener.Reset();
    _kick.Reset();
    _kickSlots.clear();
    _hook.Reset();
}

std::vector<uint64_t> Addons::RequiredFor(int64_t steamId) const
{
    std::vector<uint64_t> all = _required;
    if (const auto found = _clients.find(steamId); found != _clients.end())
    {
        for (uint64_t id : found->second.Extra)
            if (!std::ranges::contains(all, id))
                all.push_back(id);
    }
    return all;
}

std::vector<uint64_t> Addons::MissingFor(int64_t steamId) const
{
    std::vector<uint64_t> missing = RequiredFor(steamId);
    if (const auto found = _clients.find(steamId); found != _clients.end())
    {
        for (uint64_t id : found->second.Downloaded)
            std::erase(missing, id);
    }
    return missing;
}

std::vector<uint64_t> Addons::Pending(int slot) const
{
    Player* player = _players.Get(slot);
    return player ? MissingFor(player->SteamId()) : std::vector<uint64_t>{};
}

void Addons::OnConnected(Player& player)
{
    const auto found = _clients.find(player.SteamId());
    if (found != _clients.end())
    {
        ClientState& state = found->second;
        if (state.Sending != 0)
        {
            // Nothing tells us a download succeeded. Coming back promptly after being sent an
            // addon is the evidence; a client returning much later was doing something else and
            // starts that addon over.
            if (Time::MonotonicSeconds() - state.SentAt <= DownloadTimeoutSeconds)
            {
                state.Downloaded.push_back(state.Sending);
                state.Attempts = 0;
            }
            state.Sending = 0;
        }
    }

    if (MissingFor(player.SteamId()).empty())
        Ready.Raise(player.Slot());
}

void Addons::KickLater(int slot)
{
    if (std::ranges::contains(_kickSlots, slot))
        return;

    // A non-empty list already has a drain scheduled, so only the first entry schedules one. The
    // one-shot is never cancelled from inside its own callback.
    _kickSlots.push_back(slot);
    if (_kickSlots.size() > 1)
        return;

    _kick = _scheduler.Delay(0, [this] {
        for (int pending : _kickSlots)
        {
            if (_interfaces.Engine)
                _interfaces.Engine->DisconnectClient(CPlayerSlot(pending), NETWORK_DISCONNECT_TIMEDOUT,
                                                     "Required workshop addon download was declined");
        }
        _kickSlots.clear();
    });
}

bool Addons::Hook_SendNetMessage(const CNetMessage* message, int)
{
    // Every outgoing message to every client lands here, so the cheap rejections come first.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != net_SignonState)
        RETURN_META_VALUE(MRES_IGNORED, true);

    void* client = META_IFACEPTR(void);
    const int64_t steamId = client ? _bindings.ServerSideClientSteamId.Read(client) : 0;
    if (!SteamId::IsValid(steamId))
        RETURN_META_VALUE(MRES_IGNORED, true);

    std::vector<uint64_t> missing = MissingFor(steamId);
    if (missing.empty())
        RETURN_META_VALUE(MRES_IGNORED, true);

    ClientState& state = _clients[steamId];
    const uint64_t next = missing.front();

    // Same addon offered again means the last offer was not taken. Give up rather than leave the
    // client reconnecting forever.
    state.Attempts = (state.Sending == next) ? state.Attempts + 1 : 1;
    if (state.Attempts > MaxDownloadAttempts)
    {
        const int slot = SlotOfServerSideClient(_bindings, client);
        Log::Warn("Addons: {} did not take addon {} in {} attempts; dropping the client.", steamId, next,
                  MaxDownloadAttempts);
        if (IsValidSlot(slot))
            KickLater(slot);
        RETURN_META_VALUE(MRES_IGNORED, true);
    }

    state.Sending = next;
    state.SentAt = Time::MonotonicSeconds();

    // The engine owns this message and is about to serialize it; rewriting it in place is what
    // redirects the client, which is why the const is cast away here rather than in the signature.
    auto* signon = const_cast<CNetMessage*>(message)->ToPB<CNETMsg_SignonState>();
    signon->set_addons(std::to_string(next));
    signon->set_signon_state(SIGNONSTATE_CHANGELEVEL);

    Log::Info("Addons: sending addon {} to {} ({} left after it).", next, steamId, missing.size() - 1);
    RETURN_META_VALUE(MRES_IGNORED, true);
}

}  // namespace VoltMod
