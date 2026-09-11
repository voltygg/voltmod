// Time::ParseDuration delegates to VoltMod::ParseDuration; its grammar is covered by
// ParseDurationTests.
#include <VoltMod/Core/Time.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Time;

TEST_CASE("Time::ParseDuration delegates to the shared grammar")
{
    CHECK_EQ(Time::ParseDuration("5m"), static_cast<int64_t>(300));
    CHECK_EQ(Time::ParseDuration("perm"), static_cast<int64_t>(0));
    CHECK_EQ(Time::ParseDuration("garbage"), static_cast<int64_t>(-1));
}

TEST_CASE("Time::FormatDuration names the largest unit that divides exactly")
{
    CHECK_EQ(Time::FormatDuration(0), std::string("Permanent"));
    CHECK_EQ(Time::FormatDuration(1), std::string("1 second"));
    CHECK_EQ(Time::FormatDuration(45), std::string("45 seconds"));
    CHECK_EQ(Time::FormatDuration(60), std::string("1 minute"));
    CHECK_EQ(Time::FormatDuration(300), std::string("5 minutes"));
    CHECK_EQ(Time::FormatDuration(3600), std::string("1 hour"));
    CHECK_EQ(Time::FormatDuration(7200), std::string("2 hours"));
    CHECK_EQ(Time::FormatDuration(86400), std::string("1 day"));
    CHECK_EQ(Time::FormatDuration(604800), std::string("1 week"));
    CHECK_EQ(Time::FormatDuration(1209600), std::string("2 weeks"));
    // 90 minutes is not an exact hour, so it falls back to minutes.
    CHECK_EQ(Time::FormatDuration(5400), std::string("90 minutes"));
}

TEST_CASE("Time::FormatDurationLabel largest exact unit, localized")
{
    VoltMod::DurationUnitLabels units{.Permanent = "perm", .Days = "d", .Hours = "h", .Minutes = "min", .Seconds = "s"};
    CHECK_EQ(Time::FormatDurationLabel(0, units), std::string("perm"));
    CHECK_EQ(Time::FormatDurationLabel(-1, units), std::string("perm"));
    CHECK_EQ(Time::FormatDurationLabel(86400 * 3, units), std::string("3 d"));
    CHECK_EQ(Time::FormatDurationLabel(7200, units), std::string("2 h"));
    CHECK_EQ(Time::FormatDurationLabel(300, units), std::string("5 min"));
    CHECK_EQ(Time::FormatDurationLabel(90, units), std::string("90 s"));  // not an exact minute
}

TEST_CASE("Time::FormatExpiry")
{
    CHECK_EQ(Time::FormatExpiry(0, 1000, "forever", "in"), std::string("forever"));
    CHECK_EQ(Time::FormatExpiry(-1, 1000, "forever", "in"), std::string("forever"));
    CHECK_EQ(Time::FormatExpiry(900, 1000, "forever", "in"), std::string("forever"));  // already expired
    CHECK_EQ(Time::FormatExpiry(1000 + 3600, 1000, "forever", "in"), std::string("in 1 hour"));
}

TEST_CASE("Time::FormatTimestamp zero -> Never")
{
    CHECK_EQ(Time::FormatTimestamp(0), std::string("Never"));
}

TEST_CASE("Time::Now is a plausible Unix timestamp")
{
    int64_t now = Time::Now();
    // After 2025-01-01 and before 2100-01-01 - just a sanity envelope.
    CHECK(now > 1735689600LL);
    CHECK(now < 4102444800LL);
}

TEST_CASE("Time::IsExpired")
{
    CHECK(!Time::IsExpired(0));  // 0 == permanent, never expires
    CHECK(Time::IsExpired(1));   // far in the past
    CHECK(!Time::IsExpired(Time::Now() + 3600));
}

TEST_CASE("Time::GetExpirationTime")
{
    CHECK_EQ(Time::GetExpirationTime(0), static_cast<int64_t>(0));
    int64_t exp = Time::GetExpirationTime(3600);
    int64_t now = Time::Now();
    // Should be roughly an hour out (allow a small execution window).
    CHECK(exp - now >= 3599);
    CHECK(exp - now <= 3601);
}
