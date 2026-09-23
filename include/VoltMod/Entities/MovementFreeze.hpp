#pragma once

#include <VoltMod/Engine/EntityRef.hpp>
#include <VoltMod/Entities/Pawn.hpp>

namespace VoltMod
{

/**
 * @brief Holds one pawn still and gives its move type back to that pawn only, so a player who
 * respawns while held never inherits the dead body's. Keep one per player.
 */
class MovementFreeze
{
public:
    /** Freeze @p pawn. Does nothing when a pawn is already held, or when @p pawn cannot be. */
    void Hold(const Pawn& pawn);

    /** Give @p pawn its move type back if it is the one being held, then forget it. */
    void Release(const Pawn& pawn);

    /** Carry the hold across a respawn: forget a replaced pawn without writing to it, then hold
     *  @p pawn. */
    void Sync(const Pawn& pawn);

    /** Whether a pawn is being held. */
    explicit operator bool() const noexcept { return static_cast<bool>(_pawn); }

private:
    EntityRef _pawn;
    Schema::MoveType_t _prev = Schema::MoveType_t::MOVETYPE_WALK;
};

}  // namespace VoltMod
