#pragma once

#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Entities/Pawn.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Pawn-state predicate factories for state-toggle menu rows (re-read every redraw).
 */

/** The pawn is currently in @p activeType (e.g. MoveType::None = frozen). */
inline auto InMoveType(MoveType activeType)
{
    return [activeType](const Pawn& pawn) { return pawn.Move() == activeType; };
}

/** An m_fFlags bit is set on the pawn (e.g. FL_GODMODE). */
inline auto HasPawnFlag(uint32_t flag)
{
    return [flag](const Pawn& pawn) { return (pawn.Flags.Get() & flag) != 0; };
}

}  // namespace VoltMod
