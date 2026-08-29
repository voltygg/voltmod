#pragma once

#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The target-selector grammar behind a `Target()` command argument.
 *
 * Internal to command dispatch. Plugins select targets with an `Args::Target` handler parameter;
 * this implementation is not consumer API.
 *
 * Grammar: `@all`/`@*`, `@me`, `@!me`, `@t`, `@ct`, `@spec`, `@dead`, `@alive`, `@bot`,
 * `@human`, `@random`, `@randomt`, `@randomct`, `#slot`, a SteamID (64 / STEAM_ / [U:1:...]),
 * or a name fragment (exact match preferred, then prefix, then substring).
 *
 * @ref ParseTargetToken and @ref FilterRoster operate on plain @ref PlayerView records, so the
 * grammar is unit-testable without a server.
 */

/** Which target classes a command permits for one Target argument. */
struct TargetRules
{
    bool AllowMultiple = false;  ///< permit selectors (@all/@t/...) that match more than one player
    bool AllowDead = true;
    bool AllowBots = true;
};

enum class TargetError
{
    NoMatch,
    Immune,           ///< matches existed, but the policy blocked all of them
    Ambiguous,        ///< a name fragment matched more than one player
    MultiNotAllowed,  ///< a multi-selector was used where the command takes a single target
    DeadNotAllowed,
    BotNotAllowed,
};

/** Why a resolution failed; Count carries the match count for Ambiguous messages. */
struct TargetFailure
{
    TargetError Error = TargetError::NoMatch;
    int Count = 0;
};

enum class TargetKind
{
    All,
    Me,
    NotMe,
    Team,  ///< uses TargetQuery::Team (engine index: 1 = spectators, 2 = T, 3 = CT)
    Dead,
    Alive,
    Bots,
    Humans,
    Random,
    RandomTeam,  ///< uses TargetQuery::Team
    Slot,
    SteamId,
    Name,
};

/** Parsed form of one target token. */
struct TargetQuery
{
    TargetKind Kind = TargetKind::Name;
    int Team = -1;
    int Slot = -1;
    int64_t SteamId = 0;
    std::string Needle;  ///< lowercased name fragment for TargetKind::Name
};

TargetQuery ParseTargetToken(std::string_view token);

/** Engine-free snapshot of one connected player, for @ref FilterRoster. */
struct PlayerView
{
    int Slot = -1;
    int64_t SteamId = 0;
    std::string Name;
    int Team = 0;
    bool Alive = false;
    bool Bot = false;
    bool Targetable = true;  ///< policy verdict, precomputed by the caller
};

/**
 * Apply @p query + @p rules to a roster; returns the matching slots. Random kinds pick one
 * entry via @p randomIndex(count). Failures explain why a non-empty candidate set was
 * rejected (immunity, dead/bot filtering, multi/ambiguity), so callers can reply precisely.
 */
std::expected<std::vector<int>, TargetFailure> FilterRoster(
    std::span<const PlayerView> roster, const TargetQuery& query, const TargetRules& rules, int callerSlot,
    const std::function<std::size_t(std::size_t)>& randomIndex = {});

/**
 * Resolve a target token against the connected players.
 *
 * Builds a @ref PlayerView roster from @p players (pawn state through @p entities, targetability
 * through `policy.Authorize`) and delegates the grammar to @ref FilterRoster. The returned
 * players honor @p rules - a single-target command (`AllowMultiple == false`) gets exactly one
 * player or a @ref TargetFailure explaining what to tell the caller.
 *
 * @p caller null means the server console: always allowed, `@me` never matches.
 */
std::expected<std::vector<Player*>, TargetFailure> ResolveTargets(PlayerManager& players, const Policy& policy,
                                                                  EntitySystem& entities, std::string_view token,
                                                                  Player* caller, const TargetRules& rules = {});

}  // namespace VoltMod
