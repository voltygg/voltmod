#pragma once

#include <cstdint>

namespace VoltMod::Sdk
{

/**
 * @brief Subset of the CS2 `MoveType_t` schema enum (the values plugins commonly write to
 * `m_MoveType` / `m_nActualMoveType`). Extend as more values are needed.
 */
enum class MoveType : uint8_t
{
    None = 0,    ///< MOVETYPE_NONE - frozen in place.
    Walk = 2,    ///< MOVETYPE_WALK - normal player movement.
    NoClip = 7,  ///< MOVETYPE_NOCLIP - free fly-through-walls movement.
};

}  // namespace VoltMod::Sdk
