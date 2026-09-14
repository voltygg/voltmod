#include "Engine/Net/ServerSideClients.hpp"
#include "Workshop/AddonDownloads.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SteamId.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <eiface.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <utility>

namespace VoltMod
{

Addons::Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler)
    : _interfaces(interfaces),
      _bindings(bindings),
      _players(players),
      _scheduler(scheduler),
      _downloads(std::make_unique<AddonDownloads>())
{}

Addons::~Addons() = default;

Result<Subscription> Addons::Require(uint64_t id)
{
    if (id == 0)
        return std::unexpected(Error::Invalid("0 is not a workshop id"));

    if (auto hooked = EnsureHook(); !hooked)
        return std::unexpected(hooked.error());

    _downloads->Require(id);
    return Subscription([this, id] {
        _downloads->Release(id);
        UnhookIfUnused();
    });
}

Result<Subscription> Addons::RequireFor(int64_t steamId, uint64_t id)
{
    if (id == 0)
        return std::unexpected(Error::Invalid("0 is not a workshop id"));
    if (!SteamId::IsValid(steamId))
        return std::unexpected(Error::Invalid(std::format("{} is not a SteamID", steamId)));

    if (auto hooked = EnsureHook(); !hooked)
        return std::unexpected(hooked.error());

    _downloads->RequireFor(steamId, id);
    return Subscription([this, steamId, id] {
        _downloads->ReleaseFor(steamId, id);
        UnhookIfUnused();
    });
}

std::vector<uint64_t> Addons::Required() const
{
    return _downloads->Required();
}

std::vector<uint64_t> Addons::Missing(int slot) const
{
    Player* player = _players.Get(slot);
    return player ? _downloads->MissingFor(player->SteamId()) : std::vector<uint64_t>{};
}

bool Addons::HasMissing(int slot) const
{
    Player* player = _players.Get(slot);
    return player && _downloads->HasMissing(player->SteamId());
}

Status Addons::EnsureHook()
{
    if (_hook)
        return {};

    // A listen server host needs no download step.
    if (!_interfaces.Engine || !_interfaces.Engine->IsDedicatedServer())
        return std::unexpected(Error::Unsupported("addon downloads need a dedicated server"));

    auto hook = HookVTable(
        "Workshop addon delivery", _bindings.SendNetMessage,
        [this](HookedClient& client, const CNetMessage* message, int) { OnJoinMessage(message, &client); }, nullptr,
        AnyServerSideClient(_interfaces, _bindings));
    if (!hook)
        return std::unexpected(Error::Unsupported(hook.error().Detail));

    _hook = std::move(*hook);

    // A reconnect is the only sign a download finished.
    _connectListener = _players.Connected += [this](Player& player) { OnConnected(player); };
    return {};
}

void Addons::UnhookIfUnused()
{
    if (!_downloads->Empty())
        return;

    _connectListener.Reset();
    _pendingKick.ResetAll();
    _hook.Reset();
    _downloads->ClearProgress();
}

void Addons::OnConnected(Player& player)
{
    _downloads->RecordReconnect(player.SteamId(), Time::MonotonicSeconds(), DownloadTimeoutSeconds);

    if (!_downloads->HasMissing(player.SteamId()))
        Downloaded.Raise(player.Slot());
}

void Addons::KickLater(int slot, int64_t steamId)
{
    if (!IsValidSlot(slot))
        return;

    _pendingKick[slot] = _scheduler.NextTick([this, slot, steamId] {
        // The slot may have changed hands by then.
        if (!_players.Get(PlayerRef{slot, steamId}) || !_interfaces.Engine)
            return;

        _interfaces.Engine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_TIMEDOUT,
                                             "Required workshop addon download was declined");
    });
}

void Addons::OnJoinMessage(const CNetMessage* message, void* client)
{
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != net_SignonState)
        return;

    const int64_t steamId = client ? _bindings.ServerSideClientSteamId.Read(client) : 0;
    if (!SteamId::IsValid(steamId))
        return;

    // Rewritten in place: later plugins' hooks read it, then the engine serializes it.
    auto* joinMessage = const_cast<CNetMessage*>(message)->ToPB<CNETMsg_SignonState>();
    const bool reconnect = joinMessage->signon_state() == SIGNONSTATE_CHANGELEVEL;
    const AddonDecision decision = _downloads->DecideJoinMessage(steamId, reconnect, joinMessage->addons(),
                                                                 Time::MonotonicSeconds(), MaxDownloadAttempts);

    switch (decision.Action)
    {
    case AddonAction::Leave:
        return;
    case AddonAction::KeepFirst:
        Log::Info("Addons: a reconnect message named {} addons; sending {} and holding the rest.",
                  decision.Remaining + 1, decision.Id);
        joinMessage->set_addons(std::to_string(decision.Id));
        return;
    case AddonAction::DropClient:
        Log::Warn("Addons: {} did not take addon {} in {} attempts; dropping the client.", steamId, decision.Id,
                  MaxDownloadAttempts);
        KickLater(SlotOfServerSideClient(_bindings, client), steamId);
        return;
    case AddonAction::Send:
        joinMessage->set_addons(std::to_string(decision.Id));
        joinMessage->set_signon_state(SIGNONSTATE_CHANGELEVEL);
        Log::Info("Addons: sending addon {} to {} ({} left after it).", decision.Id, steamId, decision.Remaining);
        return;
    }
}

}  // namespace VoltMod
