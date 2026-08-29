#include "Commands/ArgBinding.hpp"
#include "Targeting.hpp"

#include <VoltMod/Core/Random.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>

namespace VoltMod
{

// Keep roster snapshots and SDK access here so Targeting.cpp can remain SDK-free and testable.

std::expected<std::vector<Player*>, TargetFailure> ResolveTargets(PlayerManager& players, const Policy& policy,
                                                                  EntitySystem& entities, std::string_view token,
                                                                  Player* caller, const TargetRules& rules)
{
    if (token.empty())
        return std::unexpected(TargetFailure{TargetError::NoMatch});

    std::vector<PlayerView> roster;
    roster.reserve(players.All().size());
    for (Player* player : players.All())
    {
        Pawn pawn = entities.PawnOf(player->Slot());
        roster.push_back({
            .Slot = player->Slot(),
            .SteamId = player->SteamId(),
            .Name = player->Name(),
            .Team = pawn ? static_cast<int>(pawn.Team) : 0,
            .Alive = pawn && pawn.IsAlive(),
            .Bot = player->IsBot(),
            // The command permission was already checked; this gate only checks the target.
            .Targetable = !caller || policy.Authorize(caller->Ref(), player->Ref(), {}).has_value(),
        });
    }

    auto slots = FilterRoster(roster, ParseTargetToken(token), rules, caller ? caller->Slot() : -1, RandomIndex);
    if (!slots)
        return std::unexpected(slots.error());

    std::vector<Player*> resolved;
    resolved.reserve(slots->size());
    for (int slot : *slots)
        if (Player* player = players.Get(slot))
            resolved.push_back(player);
    return resolved;
}

std::expected<std::vector<Player*>, TargetFailure> EngineArgBinder::Resolve(std::string_view token, Player* caller,
                                                                            const TargetRules& rules)
{
    return ResolveTargets(_players, _policy, _entities, token, caller, rules);
}

}  // namespace VoltMod
