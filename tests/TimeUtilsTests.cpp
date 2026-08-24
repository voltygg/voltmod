#include <VoltMod/Core/TimeUtils.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Core::TimeUtils;

TEST_CASE("TimeUtils::ParseDuration suffixes")
{
    CHECK_EQ(TimeUtils::ParseDuration("30s"), static_cast<int64_t>(30));
    CHECK_EQ(TimeUtils::ParseDuration("5m"), static_cast<int64_t>(300));
    CHECK_EQ(TimeUtils::ParseDuration("2h"), static_cast<int64_t>(7200));
    CHECK_EQ(TimeUtils::ParseDuration("7d"), static_cast<int64_t>(604800));
    CHECK_EQ(TimeUtils::ParseDuration("1w"), static_cast<int64_t>(604800));
}

TEST_CASE("TimeUtils::ParseDuration case-insensitive and bare seconds")
{
    CHECK_EQ(TimeUtils::ParseDuration("5M"), static_cast<int64_t>(300));
    CHECK_EQ(TimeUtils::ParseDuration("2H"), static_cast<int64_t>(7200));
    CHECK_EQ(TimeUtils::ParseDuration("3600"), static_cast<int64_t>(3600));
}

TEST_CASE("TimeUtils::ParseDuration permanent literals")
{
    CHECK_EQ(TimeUtils::ParseDuration("0"), static_cast<int64_t>(0));
    CHECK_EQ(TimeUtils::ParseDuration("perm"), static_cast<int64_t>(0));
    CHECK_EQ(TimeUtils::ParseDuration("permanent"), static_cast<int64_t>(0));
    CHECK_EQ(TimeUtils::ParseDuration("PERM"), static_cast<int64_t>(0));
}

TEST_CASE("TimeUtils::ParseDuration invalid -> -1 (canonical grammar)")
{
    CHECK_EQ(TimeUtils::ParseDuration(""), static_cast<int64_t>(-1));
    CHECK_EQ(TimeUtils::ParseDuration("garbage"), static_cast<int64_t>(-1));
    CHECK_EQ(TimeUtils::ParseDuration("-5"), static_cast<int64_t>(-1));
}

TEST_CASE("TimeUtils::FormatDuration")
{
    CHECK_EQ(TimeUtils::FormatDuration(0), std::string("Permanent"));
    CHECK_EQ(TimeUtils::FormatDuration(1), std::string("1 second"));
    CHECK_EQ(TimeUtils::FormatDuration(45), std::string("45 seconds"));
    CHECK_EQ(TimeUtils::FormatDuration(60), std::string("1 minute"));
    CHECK_EQ(TimeUtils::FormatDuration(300), std::string("5 minutes"));
    CHECK_EQ(TimeUtils::FormatDuration(3600), std::string("1 hour"));
    CHECK_EQ(TimeUtils::FormatDuration(7200), std::string("2 hours"));
    CHECK_EQ(TimeUtils::FormatDuration(86400), std::string("1 day"));
    CHECK_EQ(TimeUtils::FormatDuration(604800), std::string("1 week"));
    CHECK_EQ(TimeUtils::FormatDuration(1209600), std::string("2 weeks"));
}

TEST_CASE("TimeUtils::FormatDuration prefers largest exact unit")
{
    // 90 minutes is not an exact hour, so it falls back to minutes.
    CHECK_EQ(TimeUtils::FormatDuration(5400), std::string("90 minutes"));
}

TEST_CASE("TimeUtils::FormatDurationLabel largest exact unit, localized")
{
    VoltMod::Core::DurationUnitLabels units{
        .Permanent = "perm", .Days = "d", .Hours = "h", .Minutes = "min", .Seconds = "s"};
    CHECK_EQ(TimeUtils::FormatDurationLabel(0, units), std::string("perm"));
    CHECK_EQ(TimeUtils::FormatDurationLabel(-1, units), std::string("perm"));
    CHECK_EQ(TimeUtils::FormatDurationLabel(86400 * 3, units), std::string("3 d"));
    CHECK_EQ(TimeUtils::FormatDurationLabel(7200, units), std::string("2 h"));
    CHECK_EQ(TimeUtils::FormatDurationLabel(300, units), std::string("5 min"));
    CHECK_EQ(TimeUtils::FormatDurationLabel(90, units), std::string("90 s"));  // not an exact minute
}

TEST_CASE("TimeUtils::FormatExpiry")
{
    CHECK_EQ(TimeUtils::FormatExpiry(0, 1000, "forever", "in"), std::string("forever"));
    CHECK_EQ(TimeUtils::FormatExpiry(-1, 1000, "forever", "in"), std::string("forever"));
    CHECK_EQ(TimeUtils::FormatExpiry(900, 1000, "forever", "in"), std::string("forever"));  // already expired
    CHECK_EQ(TimeUtils::FormatExpiry(1000 + 3600, 1000, "forever", "in"), std::string("in 1 hour"));
}

TEST_CASE("TimeUtils::FormatTimestamp zero -> Never")
{
    CHECK_EQ(TimeUtils::FormatTimestamp(0), std::string("Never"));
}

TEST_CASE("TimeUtils::Now is a plausible Unix timestamp")
{
    int64_t now = TimeUtils::Now();
    // After 2025-01-01 and before 2100-01-01 - just a sanity envelope.
    CHECK(now > 1735689600LL);
    CHECK(now < 4102444800LL);
}

TEST_CASE("TimeUtils::IsExpired")
{
    CHECK(!TimeUtils::IsExpired(0));  // 0 == permanent, never expires
    CHECK(TimeUtils::IsExpired(1));   // far in the past
    CHECK(!TimeUtils::IsExpired(TimeUtils::Now() + 3600));
}

TEST_CASE("TimeUtils::GetExpirationTime")
{
    CHECK_EQ(TimeUtils::GetExpirationTime(0), static_cast<int64_t>(0));
    int64_t exp = TimeUtils::GetExpirationTime(3600);
    int64_t now = TimeUtils::Now();
    // Should be roughly an hour out (allow a small execution window).
    CHECK(exp - now >= 3599);
    CHECK(exp - now <= 3601);
}
