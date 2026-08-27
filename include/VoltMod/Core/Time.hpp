#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * Localized unit words for @ref Time::FormatDurationLabel. The caller supplies
 * already-translated text; the framework carries no localization of its own.
 */
struct DurationUnitLabels
{
    std::string Permanent;
    std::string Days;
    std::string Hours;
    std::string Minutes;
    std::string Seconds;
};

/** @brief Static utilities for Unix timestamps, duration parsing/formatting, and expiration checks. */
class Time
{
public:
    /**
     * Unix seconds from the wall clock - the timestamp to store, compare against a database row, or
     * show a human. NTP correction and a manual clock change can step it, backwards included, so it
     * must not be used to measure how long something took: @ref MonotonicSeconds is for that.
     */
    static int64_t Now();

    /**
     * Seconds from a monotonic clock: it never jumps and never runs backwards, so it is the one to
     * measure an elapsed interval or drive a rolling window with. The origin is arbitrary and does
     * not survive a restart - persist @ref Now instead.
     */
    static double MonotonicSeconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    /** @ref MonotonicSeconds in whole milliseconds, for the integer deadlines timers compare. */
    static int64_t MonotonicMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    /**
     * Delegates to the canonical free @ref VoltMod::ParseDuration grammar
     * (s/m/h/d/w suffixes, case-insensitive): -1 on failure, 0 for permanent.
     */
    static int64_t ParseDuration(const std::string& duration);

    static std::string FormatDuration(int64_t seconds);

    /**
     * Localized "{n} {unit}" using the largest exactly-dividing unit (days/hours/minutes,
     * falling back to seconds), or `units.Permanent` for <= 0.
     */
    static std::string FormatDurationLabel(int64_t seconds, const DurationUnitLabels& units);

    /**
     * Render an expiry timestamp as a notice suffix: `permanentText` for <= 0, otherwise
     * "{expiresInPrefix} {remaining duration}". Already-expired timestamps also render as
     * `permanentText`; callers filter expired entries before notifying.
     */
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
