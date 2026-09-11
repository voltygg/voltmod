#include <VoltMod/Engine/Memory/OffsetCheck.hpp>
#include <doctest/doctest.h>

using VoltMod::IsAlignedOffset;
using VoltMod::IsOffsetInRange;

TEST_CASE("OffsetCheck::IsOffsetInRange spans zero to the max inclusive")
{
    CHECK(IsOffsetInRange(0, 500));
    CHECK(IsOffsetInRange(500, 500));
    CHECK_FALSE(IsOffsetInRange(501, 500));
    CHECK_FALSE(IsOffsetInRange(-1, 500));
}

TEST_CASE("OffsetCheck::IsAlignedOffset needs a multiple of a positive alignment")
{
    CHECK(IsAlignedOffset(0, 4));
    CHECK(IsAlignedOffset(4096, 4));
    CHECK_FALSE(IsAlignedOffset(6, 4));

    // Alignment 1 admits everything; a non-positive one admits nothing.
    CHECK(IsAlignedOffset(4095, 1));
    CHECK_FALSE(IsAlignedOffset(0, 0));
    CHECK_FALSE(IsAlignedOffset(4, -4));
}
