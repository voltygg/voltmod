#include <VoltMod/Sdk/Engine/OffsetCheck.hpp>
#include <doctest/doctest.h>

using VoltMod::Sdk::IsAlignedOffset;
using VoltMod::Sdk::IsOffsetInRange;

TEST_CASE("OffsetCheck::IsOffsetInRange accepts the boundaries")
{
    CHECK(IsOffsetInRange(0, 500));
    CHECK(IsOffsetInRange(500, 500));
}

TEST_CASE("OffsetCheck::IsOffsetInRange rejects one past the max")
{
    CHECK_FALSE(IsOffsetInRange(501, 500));
}

TEST_CASE("OffsetCheck::IsOffsetInRange rejects negative values")
{
    CHECK_FALSE(IsOffsetInRange(-1, 500));
}

TEST_CASE("OffsetCheck::IsAlignedOffset accepts a multiple of the alignment")
{
    CHECK(IsAlignedOffset(0, 4));
    CHECK(IsAlignedOffset(4096, 4));
}

TEST_CASE("OffsetCheck::IsAlignedOffset rejects a misaligned value")
{
    CHECK_FALSE(IsAlignedOffset(6, 4));
}

TEST_CASE("OffsetCheck::IsAlignedOffset with alignment 1 accepts anything")
{
    CHECK(IsAlignedOffset(1, 1));
    CHECK(IsAlignedOffset(4095, 1));
}

TEST_CASE("OffsetCheck::IsAlignedOffset rejects alignment zero")
{
    CHECK_FALSE(IsAlignedOffset(0, 0));
}

TEST_CASE("OffsetCheck::IsAlignedOffset rejects negative alignment")
{
    CHECK_FALSE(IsAlignedOffset(4, -4));
}
