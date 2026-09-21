#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <cstdint>

namespace VoltMod
{

/** Why a round ended, as the engine's `CSRoundEndReason` numbers it; `round_end` reports the same. */
enum class RoundEndReason : uint32_t
{
    CounterTerroristsWin = 8,
    TerroristsWin = 9,
    Draw = 10,
};

/**
 * @brief Ends the current round through `CCSGameRules::TerminateRound`.
 *
 * The win panel, `round_end` and the next round's start are the engine's own, and this works
 * with `mp_ignore_round_win_conditions` on. Team scores are not changed.
 *
 * @code
 * runtime.World.Rounds.End(VoltMod::RoundEndReason::TerroristsWin, 5.0f);
 * @endcode
 *
 * Game-thread only.
 */
class Rounds
{
public:
    /** Both must outlive this service; the Runtime declares them above. */
    Rounds(EntitySystem& entities, const Bindings& bindings) : _entities(entities), _bindings(bindings) {}
    Rounds(const Rounds&) = delete;
    Rounds& operator=(const Rounds&) = delete;

    /** Unsupported when the TerminateRound signature did not bind. */
    Status Available() const;

    /** End the round for @p reason; the next one starts after @p delaySeconds.
     *  @return Error::NotReady when no map is running. */
    Status End(RoundEndReason reason, float delaySeconds) const;

private:
    EntitySystem& _entities;
    const Bindings& _bindings;
};

}  // namespace VoltMod
