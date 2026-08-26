#include <VoltMod/Core/Strings.hpp>
#include <doctest/doctest.h>

using VoltMod::Core::ParseDuration;

TEST_CASE("ParseDuration: bare seconds")
{
    CHECK_EQ(ParseDuration("30"), 30);
    CHECK_EQ(ParseDuration("1"), 1);
    CHECK_EQ(ParseDuration("3600"), 3600);
}

TEST_CASE("ParseDuration: seconds suffix")
{
    CHECK_EQ(ParseDuration("30s"), 30);
    CHECK_EQ(ParseDuration("1s"), 1);
}

TEST_CASE("ParseDuration: minutes suffix")
{
    CHECK_EQ(ParseDuration("5m"), 300);
    CHECK_EQ(ParseDuration("1m"), 60);
}

TEST_CASE("ParseDuration: hours suffix")
{
    CHECK_EQ(ParseDuration("2h"), 7200);
    CHECK_EQ(ParseDuration("1h"), 3600);
}

TEST_CASE("ParseDuration: days suffix")
{
    CHECK_EQ(ParseDuration("7d"), 604800);
    CHECK_EQ(ParseDuration("1d"), 86400);
}

TEST_CASE("ParseDuration: weeks suffix")
{
    CHECK_EQ(ParseDuration("1w"), 604800);
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
    CHECK_EQ(ParseDuration("5x"), -1);     // unknown suffix
    CHECK_EQ(ParseDuration("m"), -1);      // suffix without number
    CHECK_EQ(ParseDuration("5.5m"), -1);   // non-integer
    CHECK_EQ(ParseDuration("-5"), -1);     // negative
    CHECK_EQ(ParseDuration("12 34"), -1);  // embedded space
    CHECK_EQ(ParseDuration("5mm"), -1);    // double suffix
}

TEST_CASE("ParseDuration: case-insensitive suffixes and literals")
{
    CHECK_EQ(ParseDuration("5M"), 300);
    CHECK_EQ(ParseDuration("2H"), 7200);
    CHECK_EQ(ParseDuration("PERM"), 0);
    CHECK_EQ(ParseDuration("Permanent"), 0);
}

TEST_CASE("ParseDuration: int overflow returns -1")
{
    CHECK_EQ(ParseDuration("999999999d"), -1);
}
