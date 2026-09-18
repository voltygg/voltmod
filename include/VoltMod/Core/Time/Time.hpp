#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Translated units for @ref Time::FormatDurationLabel. */
struct DurationUnitLabels
{
    std::string Permanent;
    std::string Days;
    std::string Hours;
    std::string Minutes;
    std::string Seconds;
};

/** Timestamp, duration, and expiration utilities. */
class Time
{
public:
    /** Unix wall-clock seconds. Use @ref MonotonicSeconds for elapsed time. */
    static int64_t Now();

    /** Monotonic seconds for intervals. The arbitrary origin must not be persisted. */
    static double MonotonicSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /** Monotonic milliseconds for integer deadlines. */
    static int64_t MonotonicMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /** Parse the shared duration grammar. Returns -1 on failure and 0 for permanent. */
    static int64_t ParseDuration(const std::string& duration);

    static std::string FormatDuration(int64_t seconds);

    /** Format with the largest exact unit, or `Permanent` for non-positive values. */
    static std::string FormatDurationLabel(int64_t seconds, const DurationUnitLabels& units);

    /** Format an expiry suffix; non-positive or expired timestamps use @p permanentText. */
    static std::string FormatExpiry(int64_t expiresAt, int64_t nowSec, std::string_view permanentText,
                                    std::string_view expiresInPrefix);

    static std::string FormatTimestamp(int64_t timestamp);
    static bool IsExpired(int64_t expiresAt);
    static int64_t GetExpirationTime(int64_t durationSeconds);

private:
    static constexpr int64_t SecondsPerMinute = 60;
    static constexpr int64_t SecondsPerHour = 3600;
    static constexpr int64_t SecondsPerDay = 86400;
    static constexpr int64_t SecondsPerWeek = 604800;
};

}  // namespace VoltMod
