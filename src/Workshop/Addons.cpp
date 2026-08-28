#include "Engine/ServerSideClients.hpp"
#include "Workshop/AddonRequirements.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SteamId.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <VoltMod/Workshop/Addons.hpp>
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

/** Requirements and per-client progress; see AddonRequirements.hpp for the rules. */
class Addons::Impl
{
public:
    AddonRequirements Requirements;
};

Addons::Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler)
    : _interfaces(interfaces),
      _bindings(bindings),
      _players(players),
      _scheduler(scheduler),
      _impl(std::make_unique<Impl>())
{}

Addons::~Addons() = default;

Result<Subscription> Addons::Require(uint64_t id)
{
    if (id == 0)
        return std::unexpected(Error::Invalid("0 is not a workshop id"));

    if (auto installed = Install(); !installed)
        return std::unexpected(installed.error());

    _impl->Requirements.Require(id);
    return Subscription([this, id] {
        _impl->Requirements.Release(id);
        Remove();
    });
}

Result<Subscription> Addons::RequireFor(int64_t steamId, uint64_t id)
{
    if (id == 0)
        return std::unexpected(Error::Invalid("0 is not a workshop id"));
    if (!SteamId::IsValid(steamId))
        return std::unexpected(Error::Invalid(std::format("{} is not a SteamID", steamId)));

    if (auto installed = Install(); !installed)
        return std::unexpected(installed.error());

    _impl->Requirements.RequireFor(steamId, id);
    return Subscription([this, steamId, id] {
        _impl->Requirements.ReleaseFor(steamId, id);
        Remove();
    });
}

std::vector<uint64_t> Addons::Required() const
{
    return _impl->Requirements.Required();
}

std::vector<uint64_t> Addons::Pending(int slot) const
{
    Player* player = _players.Get(slot);
    return player ? _impl->Requirements.MissingFor(player->SteamId()) : std::vector<uint64_t>{};
}

Status Addons::Install()
{
    if (_hook)
        return {};

    // A listen server's own client already has whatever the host has, and there is no download
    // step to drive.
    if (!_interfaces.Engine || !_interfaces.Engine->IsDedicatedServer())
        return std::unexpected(Error::Unsupported("addon downloads need a dedicated server"));

    auto hook = VtableHook::OnVTable<VoltMod_SendNetMessageHook>("Workshop addon delivery", _bindings.SendNetMessage,
                                                                 this, &Addons::Hook_SendNetMessage, nullptr,
                                                                 AnyServerSideClient(_interfaces, _bindings));
    if (!hook)
        return std::unexpected(Error::Unsupported(hook.error().Detail));

    _hook = std::move(*hook);

    // A reconnect is the only signal that a download finished, so the roster drives the credit.
    _connectListener = _players.Connected += [this](Player& player) { OnConnected(player); };
    return {};
}

void Addons::Remove()
{
    if (!_impl->Requirements.Empty())
        return;

    _connectListener.Reset();
    _pendingKick.ResetAll();
    _hook.Reset();
    _impl->Requirements.ForgetClients();
}

void Addons::OnConnected(Player& player)
{
    _impl->Requirements.CreditReconnect(player.SteamId(), Time::MonotonicSeconds(), DownloadTimeoutSeconds);

    if (_impl->Requirements.MissingFor(player.SteamId()).empty())
        Ready.Raise(player.Slot());
}

void Addons::KickLater(int slot, int64_t steamId)
{
    if (!IsValidSlot(slot))
        return;

    _pendingKick[slot] = _scheduler.NextTick([this, slot, steamId] {
        // The seat can change hands before the deferred kick lands; dropping whoever took it is
        // not what the declined download says.
        if (!_players.Get(PlayerRef{slot, steamId}) || !_interfaces.Engine)
            return;

        _interfaces.Engine->DisconnectClient(CPlayerSlot(slot), NETWORK_DISCONNECT_TIMEDOUT,
                                             "Required workshop addon download was declined");
    });
}

bool Addons::Hook_SendNetMessage(const CNetMessage* message, int)
{
    HandleSignon(message, META_IFACEPTR(void));
    RETURN_META_VALUE(MRES_IGNORED, true);
}

void Addons::HandleSignon(const CNetMessage* message, void* client)
{
    // Every outgoing message to every client lands here, so the cheap rejections come first.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != net_SignonState)
        return;

    const int64_t steamId = client ? _bindings.ServerSideClientSteamId.Read(client) : 0;
    if (!SteamId::IsValid(steamId))
        return;

    // The engine owns this message and is about to serialize it; rewriting it in place is what
    // redirects the client, which is why the const is cast away here rather than in the signature.
    auto* signon = const_cast<CNetMessage*>(message)->ToPB<CNETMsg_SignonState>();
    const double now = Time::MonotonicSeconds();

    // A changelevel signon is the engine's own, and when the server mounts several addons it names
    // all of them - which the client cannot act on, leaving it in limbo with none downloaded. Cut
    // it to the first and credit that one, so it is not offered again afterwards.
    if (signon->signon_state() == SIGNONSTATE_CHANGELEVEL)
    {
        const std::vector<uint64_t> listed = ParseAddonList(signon->addons());
        if (!listed.empty())
        {
            if (listed.size() > 1)
            {
                Log::Info("Addons: the changelevel message named {} addons; sending {} and holding the rest.",
                          listed.size(), listed.front());
                signon->set_addons(std::to_string(listed.front()));
            }
            _impl->Requirements.NoteInFlight(steamId, listed.front(), now);
        }
        return;
    }

    const AddonDecision decision = _impl->Requirements.NextFor(steamId, now, MaxDownloadAttempts);
    if (decision.Step == AddonStep::Nothing)
        return;

    if (decision.Step == AddonStep::GiveUp)
    {
        Log::Warn("Addons: {} did not take addon {} in {} attempts; dropping the client.", steamId, decision.Id,
                  MaxDownloadAttempts);
        KickLater(SlotOfServerSideClient(_bindings, client), steamId);
        return;
    }

    signon->set_addons(std::to_string(decision.Id));
    signon->set_signon_state(SIGNONSTATE_CHANGELEVEL);

    Log::Info("Addons: sending addon {} to {} ({} left after it).", decision.Id, steamId, decision.Remaining);
}

}  // namespace VoltMod
