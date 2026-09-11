#include <VoltMod/Core/Strings.hpp>
#include <doctest/doctest.h>

using VoltMod::ParseDuration;

TEST_CASE("ParseDuration: a bare number is seconds and each suffix is its own unit")
{
    CHECK_EQ(ParseDuration("30"), 30);
    CHECK_EQ(ParseDuration("30s"), 30);
    CHECK_EQ(ParseDuration("5m"), 300);
    CHECK_EQ(ParseDuration("2h"), 7200);
    CHECK_EQ(ParseDuration("7d"), 604800);
    CHECK_EQ(ParseDuration("2w"), 1209600);
}

TEST_CASE("ParseDuration: permanent")
{
    CHECK_EQ(ParseDuration("0"), 0);
    CHECK_EQ(ParseDuration("perm"), 0);
    CHECK_EQ(ParseDuration("permanent"), 0);
}

TEST_CASE("ParseDuration: surrounding whitespace")
{
    CHECK_EQ(ParseDuration("  30s  "), 30);
    CHECK_EQ(ParseDuration("\t5m\n"), 300);
    CHECK_EQ(ParseDuration("  perm  "), 0);
    CHECK_EQ(ParseDuration("  42  "), 42);
}

TEST_CASE("ParseDuration: invalid inputs return -1")
{
    CHECK_EQ(ParseDuration(""), -1);
    CHECK_EQ(ParseDuration("   "), -1);
    CHECK_EQ(ParseDuration("abc"), -1);
    CHECK_EQ(ParseDuration("5x"), -1);           // unknown suffix
    CHECK_EQ(ParseDuration("m"), -1);            // suffix without number
    CHECK_EQ(ParseDuration("5.5m"), -1);         // non-integer
    CHECK_EQ(ParseDuration("-5"), -1);           // negative
    CHECK_EQ(ParseDuration("12 34"), -1);        // embedded space
    CHECK_EQ(ParseDuration("5mm"), -1);          // double suffix
    CHECK_EQ(ParseDuration("999999999d"), -1);   // overflows int
}

TEST_CASE("ParseDuration: case-insensitive suffixes and literals")
{
    CHECK_EQ(ParseDuration("5M"), 300);
    CHECK_EQ(ParseDuration("2H"), 7200);
    CHECK_EQ(ParseDuration("PERM"), 0);
    CHECK_EQ(ParseDuration("Permanent"), 0);
}
