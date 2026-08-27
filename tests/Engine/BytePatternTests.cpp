#include "Engine/BytePattern.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using VoltMod::AnchorOf;
using VoltMod::ByteHistogram;
using VoltMod::CountBytes;
using VoltMod::FindFirst;
using VoltMod::ParsePattern;
using VoltMod::PatternByte;

static ByteHistogram Frequencies(const std::vector<uint8_t>& memory)
{
    ByteHistogram counts{};
    CountBytes(memory.data(), memory.size(), counts);
    return counts;
}

/** The match offset, or -1. Keeps the cases readable. */
static ptrdiff_t Find(const std::vector<uint8_t>& memory, const std::string& pattern)
{
    const auto bytes = ParsePattern(pattern);
    const auto* hit = FindFirst(memory.data(), memory.size(), bytes, AnchorOf(bytes, Frequencies(memory)));
    return hit ? hit - memory.data() : -1;
}

TEST_CASE("ParsePattern reads hex bytes and both wildcard spellings")
{
    const auto bytes = ParsePattern("48 8B ? 05 ??");
    REQUIRE(bytes.size() == 5);

    CHECK(bytes[0].Value == 0x48);
    CHECK_FALSE(bytes[0].Wildcard);
    CHECK(bytes[2].Wildcard);
    CHECK(bytes[3].Value == 0x05);
    CHECK(bytes[4].Wildcard);
}

TEST_CASE("ParsePattern rejects the whole pattern when a token is not a byte")
{
    // Dropping the signature is the point: half a pattern would match something else entirely.
    CHECK(ParsePattern("48 ZZ 05").empty());
    CHECK(ParsePattern("48 1FF").empty());
    CHECK(ParsePattern("48 -1").empty());
    CHECK(ParsePattern("").empty());
}

TEST_CASE("AnchorOf picks the rarest concrete byte, not the first")
{
    // 0x48 is everywhere, 0x9C occurs once - the whole reason the anchor is chosen by frequency.
    std::vector<uint8_t> memory(256, 0x48);
    memory[100] = 0x9C;

    const auto bytes = ParsePattern("48 9C");
    CHECK(AnchorOf(bytes, Frequencies(memory)) == 1);
}

TEST_CASE("AnchorOf reports no anchor for an all-wildcard pattern")
{
    const auto bytes = ParsePattern("? ? ?");
    CHECK(AnchorOf(bytes, ByteHistogram{}) == bytes.size());
}

TEST_CASE("FindFirst locates a pattern at the start, middle and end of the memory")
{
    const std::vector<uint8_t> memory{0xAA, 0xBB, 0xCC, 0xDD, 0xEE};

    CHECK(Find(memory, "AA BB") == 0);
    CHECK(Find(memory, "CC DD") == 2);
    CHECK(Find(memory, "EE") == 4);
    CHECK(Find(memory, "AA BB CC DD EE") == 0);
}

TEST_CASE("FindFirst returns the first of several matches")
{
    const std::vector<uint8_t> memory{0x90, 0x11, 0x22, 0x90, 0x11, 0x22};
    CHECK(Find(memory, "11 22") == 1);
}

TEST_CASE("FindFirst treats a wildcard as matching any byte")
{
    const std::vector<uint8_t> memory{0x48, 0x8B, 0x37, 0x05};

    CHECK(Find(memory, "48 8B ? 05") == 0);
    CHECK(Find(memory, "48 ? ? 05") == 0);
    // A wildcard still occupies a position, so the pattern must fit at that offset.
    CHECK(Find(memory, "8B ? 05") == 1);
}

TEST_CASE("FindFirst reports nothing when the pattern is absent or does not fit")
{
    const std::vector<uint8_t> memory{0x01, 0x02, 0x03};

    CHECK(Find(memory, "01 03") == -1);
    CHECK(Find(memory, "01 02 03 04") == -1);  // longer than the memory
    CHECK(Find(memory, "FF") == -1);
}

TEST_CASE("FindFirst matches an all-wildcard pattern at the first offset")
{
    const std::vector<uint8_t> memory{0x01, 0x02, 0x03};
    CHECK(Find(memory, "? ?") == 0);
}

TEST_CASE("FindFirst handles an empty pattern and empty memory without reading anything")
{
    const std::vector<uint8_t> memory{0x01, 0x02};

    CHECK(FindFirst(memory.data(), memory.size(), {}, 0) == nullptr);
    CHECK(FindFirst(nullptr, 0, ParsePattern("01"), 0) == nullptr);
    CHECK(FindFirst(memory.data(), 0, ParsePattern("01"), 0) == nullptr);
}

TEST_CASE("FindFirst gives the same answer whichever concrete byte anchors it")
{
    // The anchor is a speed choice, never a correctness one; every valid one must agree.
    const std::vector<uint8_t> memory{0x00, 0x48, 0x8B, 0x37, 0x05, 0x48, 0x8B, 0x99, 0x05};
    const auto bytes = ParsePattern("48 8B ? 05");

    const auto* expected = FindFirst(memory.data(), memory.size(), bytes, 0);
    REQUIRE(expected == memory.data() + 1);

    CHECK(FindFirst(memory.data(), memory.size(), bytes, 1) == expected);
    CHECK(FindFirst(memory.data(), memory.size(), bytes, 3) == expected);
}
