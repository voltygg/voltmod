#pragma once

#include <VoltMod/Entities/Pawn.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Pawn-state predicate factories for state-toggle menu rows (re-read every redraw).
 */

/** The pawn is currently in @p activeType (e.g. MOVETYPE_NONE = frozen). */
inline auto InMoveType(Schema::MoveType_t activeType)
{
    return [activeType](const Pawn& pawn) { return pawn.MoveType() == activeType; };
}

/** An m_fFlags bit is set on the pawn (e.g. FL_GODMODE). */
inline auto HasPawnFlag(uint32_t flag)
{
    return [flag](const Pawn& pawn) { return (pawn.Flags() & flag) != 0; };
}

}  // namespace VoltMod
