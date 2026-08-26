#include "CommandRouter.hpp"
#include "Targeting.hpp"

#include <VoltMod/Core/Random.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>

namespace VoltMod
{

// The engine half of the selector grammar: it snapshots the roster, which needs pawns and so the
// SDK. Everything it delegates to lives in Targeting.cpp, which stays SDK-free and unit-tested.

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
            // The same gate the command itself went through, with no permission to check: the
            // spec's permission was already decided for the caller, this asks only whether the
            // caller may act on this one player.
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
