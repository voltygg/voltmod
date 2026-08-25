#pragma once

#include <VoltMod/Sdk/Entity/MoveType.hpp>
#include <VoltMod/Sdk/Entity/PlayerController.hpp>
#include <cstdint>

namespace VoltMod::Sdk
{

/**
 * @brief Pawn-state predicate factories for state-toggle menu rows (re-read every redraw).
 */

/** The pawn is currently in @p activeType (e.g. MoveType::None = frozen). */
inline auto InMoveType(MoveType activeType)
{
    return [activeType](const PlayerController& pc) { return pc.GetMoveType() == activeType; };
}

/** An m_fFlags bit is set on the pawn (e.g. FL_GODMODE). */
inline auto HasPawnFlag(uint32_t flag)
{
    return [flag](const PlayerController& pc) { return (pc.GetFlags() & flag) != 0; };
}

}  // namespace VoltMod::Sdk
