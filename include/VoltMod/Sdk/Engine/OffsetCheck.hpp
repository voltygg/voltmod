#pragma once

namespace VoltMod::Sdk
{

/**
 * @file OffsetCheck.hpp
 * @brief Pure bounds checks for GameData's offset accessors, split into the two halves it
 * reports separately so a rejection says whether the value was out of range or misaligned.
 */

/** True when @p value is within [0, max]. */
constexpr bool IsOffsetInRange(int value, int max) noexcept
{
    return value >= 0 && value <= max;
}

/** True when @p value is a multiple of @p alignment. A non-positive alignment is never valid. */
constexpr bool IsAlignedOffset(int value, int alignment) noexcept
{
    return alignment > 0 && value % alignment == 0;
}

}  // namespace VoltMod::Sdk
