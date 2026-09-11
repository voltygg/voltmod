#pragma once

#include <cstddef>
#include <cstdint>

namespace VoltMod
{

/**
 * @file RelativeAddress.hpp
 * @brief The RIP-relative arithmetic the signature scanner does, as pure functions.
 *
 * A rel32 operand encodes the distance from the *end* of the instruction to its target. Getting
 * the site, the operand width, or the bounds check wrong yields a plausible-looking pointer into
 * unrelated memory, so the arithmetic lives here where it can be checked without a loaded module.
 */

/** Width of the displacement these helpers read. */
inline constexpr int Rel32Size = 4;

/** Address of the 4-byte displacement belonging to a match at @p matchAddress. */
constexpr uintptr_t Rel32Site(uintptr_t matchAddress, int ripOffset) noexcept
{
    return matchAddress + static_cast<uintptr_t>(ripOffset);
}

/**
 * Absolute target of the displacement read at @p site.
 *
 * @p ripSize is the distance from @p site to the first byte after the instruction - 4 for a plain
 * rel32 operand that ends the instruction.
 */
constexpr uintptr_t Rel32Target(uintptr_t site, int32_t displacement, int ripSize = Rel32Size) noexcept
{
    return site + static_cast<uintptr_t>(ripSize) + static_cast<uintptr_t>(static_cast<intptr_t>(displacement));
}

/**
 * True when the whole 4-byte displacement for @p matchAddress lies inside the mapped image
 * [@p moduleBase, @p moduleBase + @p moduleSize).
 *
 * A pattern that matches near the end of a module, or a `rel32At` larger than the instruction, is
 * otherwise a read past the mapping.
 */
constexpr bool Rel32ReadInBounds(uintptr_t moduleBase, size_t moduleSize, uintptr_t matchAddress,
                                 int ripOffset) noexcept
{
    if (ripOffset < 0 || matchAddress < moduleBase)
        return false;

    const uintptr_t moduleEnd = moduleBase + moduleSize;
    const uintptr_t site = Rel32Site(matchAddress, ripOffset);
    if (site < matchAddress || site >= moduleEnd)  // overflowed, or already past the end
        return false;

    return moduleEnd - site >= static_cast<uintptr_t>(Rel32Size);
}

}  // namespace VoltMod
