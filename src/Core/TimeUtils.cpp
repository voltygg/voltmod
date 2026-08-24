#include <VoltMod/Core/StringUtils.hpp>
#include <VoltMod/Core/TimeUtils.hpp>
#include <charconv>
#include <chrono>
#include <format>

namespace VoltMod::Core
{

int64_t TimeUtils::Now()
{
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
}

int64_t TimeUtils::ParseDuration(const std::string& duration)
{
    return ::VoltMod::Core::ParseDuration(duration);
}

std::string TimeUtils::FormatDuration(int64_t seconds)
{
    if (seconds == 0)
        return "Permanent";

    if (seconds % SecondsPerWeek == 0 && seconds >= SecondsPerWeek)
    {
        int64_t w = seconds / SecondsPerWeek;
        return std::format("{} week{}", w, w > 1 ? "s" : "");
    }
    if (seconds % SecondsPerDay == 0 && seconds >= SecondsPerDay)
    {
        int64_t d = seconds / SecondsPerDay;
        return std::format("{} day{}", d, d > 1 ? "s" : "");
    }
    if (seconds % SecondsPerHour == 0 && seconds >= SecondsPerHour)
    {
        int64_t h = seconds / SecondsPerHour;
        return std::format("{} hour{}", h, h > 1 ? "s" : "");
    }
    if (seconds % SecondsPerMinute == 0 && seconds >= SecondsPerMinute)
    {
        int64_t m = seconds / SecondsPerMinute;
        return std::format("{} minute{}", m, m > 1 ? "s" : "");
    }
    return std::format("{} second{}", seconds, seconds > 1 ? "s" : "");
}

std::string TimeUtils::FormatDurationLabel(int64_t seconds, const DurationUnitLabels& units)
{
    if (seconds <= 0)
        return units.Permanent;

    struct Unit
    {
        int64_t Divisor;
        const std::string* Label;
    };
    const Unit unitTable[] = {
        {SecondsPerDay, &units.Days},
        {SecondsPerHour, &units.Hours},
        {SecondsPerMinute, &units.Minutes},
    };
    for (const auto& unit : unitTable)
    {
        if (seconds % unit.Divisor == 0)
            return std::format("{} {}", seconds / unit.Divisor, *unit.Label);
    }
    return std::format("{} {}", seconds, units.Seconds);
}

std::string TimeUtils::FormatExpiry(int64_t expiresAt, int64_t nowSec, std::string_view permanentText,
                                    std::string_view expiresInPrefix)
{
    if (expiresAt <= 0)
        return std::string(permanentText);

    int64_t remaining = expiresAt - nowSec;
    if (remaining <= 0)
        return std::string(permanentText);

    return std::format("{} {}", expiresInPrefix, FormatDuration(remaining));
}

std::string TimeUtils::FormatTimestamp(int64_t timestamp)
{
    if (timestamp == 0)
        return "Never";

    std::time_t time = static_cast<std::time_t>(timestamp);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    return std::format("{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                       tm.tm_hour, tm.tm_min, tm.tm_sec);
}

bool TimeUtils::IsExpired(int64_t expiresAt)
{
    if (expiresAt == 0)
        return false;
    return Now() >= expiresAt;
}

int64_t TimeUtils::GetExpirationTime(int64_t durationSeconds)
{
    if (durationSeconds == 0)
        return 0;
    return Now() + durationSeconds;
}

}  // namespace VoltMod::Core
