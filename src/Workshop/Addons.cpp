#include "Engine/Net/ServerSideClients.hpp"
#include "Workshop/AddonDownloads.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SteamId.hpp>
#include <VoltMod/Core/Time/Time.hpp>
#include <VoltMod/Engine/Memory/MemoryAccess.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <eiface.h>
#include <networkbasetypes.pb.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string>
#include <tier1/utlstring.h>
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

    if (auto hooked = InstallHooks(); !hooked)
        return std::unexpected(hooked.error());

    _downloads->Require(id);
    return Subscription([this, id] {
        _downloads->Release(id);
        RemoveHooksIfUnused();
    });
}

Result<Subscription> Addons::RequireFor(int64_t steamId, uint64_t id)
{
    if (id == 0)
        return std::unexpected(Error::Invalid("0 is not a workshop id"));
    if (!SteamId::IsValid(steamId))
        return std::unexpected(Error::Invalid(std::format("{} is not a SteamID", steamId)));

    if (auto hooked = InstallHooks(); !hooked)
        return std::unexpected(hooked.error());

    _downloads->RequireFor(steamId, id);
    return Subscription([this, steamId, id] {
        _downloads->ReleaseFor(steamId, id);
        RemoveHooksIfUnused();
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

Status Addons::InstallHooks()
{
    if (_joinMessageHook)
        return {};

    // A listen server host needs no download step.
    if (!_interfaces.Engine || !_interfaces.Engine->IsDedicatedServer())
        return std::unexpected(Error::Unsupported("addon downloads need a dedicated server"));
    if (!_bindings.ClientSteamId || !_bindings.ServerAddons)
        return std::unexpected(Error::Unsupported("the client SteamID or server addons offset did not bind"));

    auto join =
        HookVirtual("Workshop addon download", _bindings.SendNetMessage,
                    [this](EngineClient& client, const CNetMessage* message, int) { OnJoinMessage(message, &client); });
    if (!join)
        return std::unexpected(Error::Unsupported(join.error().Detail));

    auto reply = HookFunction(
        "Workshop addon mount", _bindings.ReplyConnection,
        [this](EngineServer& server, EngineClient* client) { AddToReply(server, client); },
        [this](EngineServer& server, EngineClient*) { RestoreReply(server); });
    if (!reply)
        return std::unexpected(Error::Unsupported(reply.error().Detail));

    _joinMessageHook = std::move(*join);
    _connectionReplyHook = std::move(*reply);

    // A reconnect is the only sign a download finished.
    _connectListener = _players.Connected += [this](Player& player) { OnConnected(player); };
    return {};
}

void Addons::RemoveHooksIfUnused()
{
    if (!_downloads->Empty())
        return;

    _connectListener.Reset();
    _pendingKick.ResetAll();
    _joinMessageHook.Reset();
    _connectionReplyHook.Reset();
    _addedToReply.clear();
    _downloads->ClearProgress();
}

static CUtlString* AddonList(const Bindings& bindings, EngineServer& server)
{
    return MemberPtr<CUtlString>(&server, bindings.ServerAddons.Value());
}

void Addons::AddToReply(EngineServer& server, const EngineClient* client)
{
    const int64_t steamId = _bindings.ClientSteamId.Read(client);
    if (!SteamId::IsValid(steamId))
        return;

    const std::vector<uint64_t> toMount = _downloads->ToMount(steamId);
    if (toMount.empty())
        return;

    // The client mounts only what the connection reply names.
    CUtlString* list = AddonList(_bindings, server);
    std::string field = list->Get();
    _addedToReply = AppendToAddonList(field, toMount);
    if (_addedToReply.empty())
        return;

    list->Set(field.c_str());
    Log::Info("Addons: telling {} to mount {}.", steamId, field);
}

void Addons::RestoreReply(EngineServer& server)
{
    if (_addedToReply.empty())
        return;

    // Only our entries; other plugins' and the map's stay.
    CUtlString* list = AddonList(_bindings, server);
    std::string field = list->Get();
    RemoveFromAddonList(field, _addedToReply);
    list->Set(field.c_str());
    _addedToReply.clear();
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

    const int64_t steamId = _bindings.ClientSteamId.Read(client);
    if (!SteamId::IsValid(steamId))
        return;

    // Rewritten in place: later plugins' hooks read it, then the engine serializes it.
    auto* joinMessage = const_cast<CNetMessage*>(message)->ToPB<CNETMsg_SignonState>();
    const bool reconnect = joinMessage->signon_state() == SIGNONSTATE_CHANGELEVEL;
    const AddonDecision decision = _downloads->DecideJoinMessage(steamId, reconnect, joinMessage->addons(),
                                                                 Time::MonotonicSeconds(), MaxDownloadAttempts);

    switch (decision.Action)
    {
    case AddonAction::Unchanged:
        return;
    case AddonAction::TrimToFirst:
        Log::Info("Addons: a reconnect message named {} addons; sending {} and holding the rest.",
                  decision.Remaining + 1, decision.Id);
        joinMessage->set_addons(std::to_string(decision.Id));
        return;
    case AddonAction::Kick:
        Log::Warn("Addons: {} did not take addon {} in {} attempts; dropping the client.", steamId, decision.Id,
                  MaxDownloadAttempts);
        KickLater(SlotOfClient(_bindings, client), steamId);
        return;
    case AddonAction::Send:
        joinMessage->set_addons(std::to_string(decision.Id));
        joinMessage->set_signon_state(SIGNONSTATE_CHANGELEVEL);
        Log::Info("Addons: sending addon {} to {} ({} left after it).", decision.Id, steamId, decision.Remaining);
        return;
    }
}

}  // namespace VoltMod
