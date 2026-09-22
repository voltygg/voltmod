#pragma once

#include <cstddef>
#include <cstdint>

namespace VoltMod
{

/**
 * @file RelativeAddress.hpp
 * @brief Checked rel32 decoding for signature bindings.
 *
 * The displacement starts at `match + ripOffset` and is relative to the end of the instruction.
 * Keeping the arithmetic pure makes malformed offsets testable without reading process memory.
 */

inline constexpr int Rel32Size = 4;

constexpr uintptr_t Rel32Site(uintptr_t matchAddress, int ripOffset) noexcept
{
    return matchAddress + static_cast<uintptr_t>(ripOffset);
}

/**
 * Resolve @p displacement from @p site. @p ripSize is the distance from the site to the end of the
 * instruction and defaults to the four-byte displacement width.
 */
constexpr uintptr_t Rel32Target(uintptr_t site, int32_t displacement, int ripSize = Rel32Size) noexcept
{
    return site + static_cast<uintptr_t>(ripSize) + static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
}

/**
 * Whether the four-byte displacement at `matchAddress + ripOffset` fits entirely within the
 * mapped image. Invalid and overflowing offsets return false before any read occurs.
 */
constexpr bool Rel32ReadInBounds(uintptr_t moduleBase, size_t moduleSize, uintptr_t matchAddress,
                                 int ripOffset) noexcept
{
    if (ripOffset < 0 || matchAddress < moduleBase)
    {
        return false;
    }

    const uintptr_t moduleEnd = moduleBase + moduleSize;
    const uintptr_t site = Rel32Site(matchAddress, ripOffset);
    if (site < matchAddress || site >= moduleEnd)
    {
        return false;
    }

    return moduleEnd - site >= static_cast<uintptr_t>(Rel32Size);
}

}  // namespace VoltMod
