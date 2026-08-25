#include <VoltMod/Sdk/Engine/OffsetCheck.hpp>
#include <doctest/doctest.h>

using VoltMod::Sdk::IsPlausibleByteOffset;
using VoltMod::Sdk::IsPlausibleVtableIndex;

TEST_CASE("OffsetCheck::IsPlausibleVtableIndex accepts the boundaries")
{
    CHECK(IsPlausibleVtableIndex(0, 500));
    CHECK(IsPlausibleVtableIndex(500, 500));
}

TEST_CASE("OffsetCheck::IsPlausibleVtableIndex rejects one past the max")
{
    CHECK_FALSE(IsPlausibleVtableIndex(501, 500));
}

TEST_CASE("OffsetCheck::IsPlausibleVtableIndex rejects negative values")
{
    CHECK_FALSE(IsPlausibleVtableIndex(-1, 500));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset accepts the boundaries")
{
    CHECK(IsPlausibleByteOffset(0, 4096, 4));
    CHECK(IsPlausibleByteOffset(4096, 4096, 4));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset rejects one past the max")
{
    CHECK_FALSE(IsPlausibleByteOffset(4097, 4096, 1));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset rejects negative values")
{
    CHECK_FALSE(IsPlausibleByteOffset(-4, 4096, 4));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset rejects a misaligned value")
{
    CHECK_FALSE(IsPlausibleByteOffset(6, 4096, 4));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset with alignment 1 accepts anything in range")
{
    CHECK(IsPlausibleByteOffset(1, 4096, 1));
    CHECK(IsPlausibleByteOffset(4095, 4096, 1));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset rejects alignment zero")
{
    CHECK_FALSE(IsPlausibleByteOffset(0, 4096, 0));
}

TEST_CASE("OffsetCheck::IsPlausibleByteOffset rejects negative alignment")
{
    CHECK_FALSE(IsPlausibleByteOffset(4, 4096, -4));
}
