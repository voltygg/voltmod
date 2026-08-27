#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @file BytePattern.hpp
 * @brief Parsing and searching byte patterns, with no notion of a loaded module.
 *
 * The half of signature scanning that is plain buffer work, split from @ref SigScanner so it is
 * unit-tested without a mapped module behind it. SigScanner supplies the memory to search and the
 * frequencies to weigh; everything here is a pure function of its arguments.
 */

/** One byte of a parsed pattern. A wildcard matches whatever is there. */
struct PatternByte
{
    uint8_t Value = 0;
    bool Wildcard = false;
};

/** How often each byte value occurs in the memory being searched. */
using ByteHistogram = std::array<size_t, 256>;

/**
 * Parse a pattern like `48 8B ? ? 05` into bytes and wildcards.
 *
 * @return the bytes, or empty when a token is neither a hex byte nor `?`. That is reported here
 *         and leaves the caller dropping the signature, rather than searching for something the
 *         author did not write.
 */
std::vector<PatternByte> ParsePattern(const std::string& pattern);

/** Add the byte values in [base, base + size) to @p counts. */
void CountBytes(const uint8_t* base, size_t size, ByteHistogram& counts);

/**
 * Index of the pattern byte to search for, or `pattern.size()` when it is all wildcards.
 *
 * Which byte is searched for is what decides a scan's cost, because the rest of the pattern is
 * only compared where that byte lands. The first byte - the obvious anchor - is close to the worst
 * one for x86-64: nearly every signature opens with a REX prefix (0x48), which saturates the
 * image. Measured over CS2's server.dll, anchoring on the rarest byte instead scans ~16x faster.
 */
size_t AnchorOf(const std::vector<PatternByte>& pattern, const ByteHistogram& frequencies);

/**
 * First match of @p pattern in [base, base + size), searched for by its @p anchor byte.
 *
 * @param anchor from @ref AnchorOf, against the frequencies of the memory being searched. Any
 *               index of a non-wildcard byte gives the same answer; only the speed differs.
 * @return the match, or nullptr.
 */
const uint8_t* FindFirst(const uint8_t* base, size_t size, const std::vector<PatternByte>& pattern, size_t anchor);

}  // namespace VoltMod
