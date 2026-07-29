#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace CS2Kit::Utils
{

/**
 * Seconds from a monotonic clock, for measuring elapsed time and driving rolling windows.
 * Unrelated to @ref TimeUtils::Now, whose wall-clock epoch is what belongs in a database.
 */
inline double MonotonicSeconds()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * Localized unit words for @ref TimeUtils::FormatDurationLabel. The caller supplies
 * already-translated text; the kit carries no localization of its own.
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
class TimeUtils
{
public:
    static int64_t Now();

    /**
     * Delegates to the canonical free @ref CS2Kit::Utils::ParseDuration grammar
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

}  // namespace CS2Kit::Utils
