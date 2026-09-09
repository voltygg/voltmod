#pragma once

#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/Pawn.hpp>

namespace VoltMod
{

/**
 * @brief Holds one pawn still and gives back only that pawn's move type.
 *
 * The subtle part is who is released: a player who dies and respawns while held must not inherit
 * the dead body's move type, so the release is keyed on the pawn that was actually frozen rather
 * than on whoever occupies the slot now. Anything that pins a player while a screen is up - the
 * built-in menu, a plugin's own - keeps one of these per player.
 */
class MovementFreeze
{
public:
    /** Freeze @p pawn. Does nothing when a pawn is already held, or when @p pawn cannot be. */
    void Hold(const Pawn& pawn);

    /** Give @p pawn its move type back if it is the one being held, then forget it. */
    void Release(const Pawn& pawn);

    /** Re-apply across a respawn: let go of a pawn that died or was replaced without writing to
     *  it, then hold @p pawn. */
    void Sync(const Pawn& pawn);

    /** Whether a pawn is being held. */
    explicit operator bool() const noexcept { return static_cast<bool>(_pawn); }

private:
    EntityRef _pawn;
    MoveType _prev = MoveType::Walk;
};

}  // namespace VoltMod
