#include <VoltMod/Core/Random.hpp>
#include <VoltMod/Entities/PlayerController.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/TargetResolver.hpp>
#include <VoltMod/Runtime.hpp>

namespace VoltMod
{

std::expected<std::vector<Player*>, TargetFailure> ResolveTargets(Runtime& runtime, std::string_view token,
                                                                  Player* caller, const TargetRules& rules,
                                                                  const CanTargetFn& canTarget)
{
    if (token.empty())
        return std::unexpected(TargetFailure{TargetError::NoMatch});

    auto& mgr = runtime.Players;
    auto& entities = runtime.Entities;
    const CanTargetFn& policy = canTarget ? canTarget : runtime.Policy.CanTarget;

    // Snapshot the roster into engine-free views; FilterRoster owns the grammar semantics.
    std::vector<PlayerView> roster;
    for (auto* p : mgr.GetAllPlayers())
    {
        if (!p)
            continue;
        PlayerController ctrl = entities.Controller(p->GetSlot());
        roster.push_back({
            .Slot = p->GetSlot(),
            .SteamId = p->GetSteamID(),
            .Name = p->GetName(),
            .Team = ctrl.IsValid() ? ctrl.GetTeam() : 0,
            .Alive = ctrl.IsValid() && ctrl.IsAlive(),
            .Bot = p->IsBot(),
            .Targetable = (caller && policy) ? policy(*caller, *p) : true,
        });
    }

    auto slots = FilterRoster(roster, ParseTargetToken(token), rules, caller ? caller->GetSlot() : -1, RandomIndex);
    if (!slots)
        return std::unexpected(slots.error());

    std::vector<Player*> players;
    players.reserve(slots->size());
    for (int slot : *slots)
        if (auto* p = mgr.GetPlayerBySlot(slot))
            players.push_back(p);
    return players;
}

}  // namespace VoltMod
