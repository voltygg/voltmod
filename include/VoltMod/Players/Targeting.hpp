#pragma once

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
 * @brief Engine-free core of the target-selector grammar: token parsing and roster filtering.
 *
 * The engine-facing @ref ResolveTargets (TargetResolver.hpp) builds a @ref PlayerView roster
 * from live players and delegates here, so the grammar and rule semantics are unit-testable
 * without the SDK.
 *
 * Grammar: `@all`/`@*`, `@me`, `@!me`, `@t`, `@ct`, `@spec`, `@dead`, `@alive`, `@bot`,
 * `@human`, `@random`, `@randomt`, `@randomct`, `#slot`, a SteamID (64 / STEAM_ / [U:1:...]),
 * or a name fragment (exact match preferred, then prefix, then substring).
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
    Immune,           ///< matches existed, but the targetability policy blocked all of them
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
    bool Targetable = true;  ///< immunity-policy verdict, precomputed by the caller
};

/**
 * Apply @p query + @p rules to a roster; returns the matching slots. Random kinds pick one
 * entry via @p randomIndex(count). Failures explain why a non-empty candidate set was
 * rejected (immunity, dead/bot filtering, multi/ambiguity), so callers can reply precisely.
 */
std::expected<std::vector<int>, TargetFailure> FilterRoster(
    std::span<const PlayerView> roster, const TargetQuery& query, const TargetRules& rules, int callerSlot,
    const std::function<std::size_t(std::size_t)>& randomIndex = {});

}  // namespace VoltMod
