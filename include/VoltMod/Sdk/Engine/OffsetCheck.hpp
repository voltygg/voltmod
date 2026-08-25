#pragma once

namespace VoltMod::Sdk
{

/**
 * @file OffsetCheck.hpp
 * @brief Pure bounds checks shared by GameData's vtable index and byte offset accessors.
 */

/** True when @p value is a plausible vtable slot: within [0, maxIndex]. */
constexpr bool IsPlausibleVtableIndex(int value, int maxIndex) noexcept
{
    return value >= 0 && value <= maxIndex;
}

/** True when @p value is a plausible byte offset: within [0, maxBytes] and a multiple of @p alignment. */
constexpr bool IsPlausibleByteOffset(int value, int maxBytes, int alignment) noexcept
{
    return value >= 0 && value <= maxBytes && alignment > 0 && value % alignment == 0;
}

}  // namespace VoltMod::Sdk
