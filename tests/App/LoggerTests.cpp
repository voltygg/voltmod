#include <VoltMod/App/Logger.hpp>
#include <doctest/doctest.h>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using VoltMod::Logger;
using VoltMod::LogLevel;

class BhopManager
{};

namespace Reports
{
class ReportQueue
{};
}  // namespace Reports

struct Outer
{
    struct Session
    {};
};

/** Counts its own formatting, so a dropped line can be shown to cost nothing. */
struct CountedArgument
{
    static inline int Formats = 0;
};

template <>
struct std::formatter<CountedArgument> : std::formatter<std::string_view>
{
    auto format(const CountedArgument&, std::format_context& context) const
    {
        ++CountedArgument::Formats;
        return std::formatter<std::string_view>::format("counted", context);
    }
};

/** Collects lines for one test and leaves VoltMod::Log:: as it was found. */
struct LogRecorder
{
    std::vector<std::string> Lines;

    LogRecorder()
    {
        VoltMod::Log::SetHandler([this](LogLevel, std::string_view message) { Lines.emplace_back(message); });
    }

    ~LogRecorder()
    {
        VoltMod::Log::SetHandler({});
        VoltMod::Log::SetMinimumLevel(LogLevel::Info);
    }
};

TEST_CASE("Logger puts the type name in front of the message")
{
    LogRecorder recorder;
    Logger<BhopManager> log;

    log.Info("ready in {}ms", 12);
    log.Warn("slow tick");
    log.Error("gone");

    REQUIRE(recorder.Lines.size() == 3);
    CHECK(recorder.Lines[0] == "[BhopManager] ready in 12ms");
    CHECK(recorder.Lines[1] == "[BhopManager] slow tick");
    CHECK(recorder.Lines[2] == "[BhopManager] gone");
}

TEST_CASE("Logger names a namespaced or nested type by its class name alone")
{
    LogRecorder recorder;
    Logger<Reports::ReportQueue> namespaced;
    Logger<Outer::Session> nested;

    namespaced.Info("drained");
    nested.Info("open");

    REQUIRE(recorder.Lines.size() == 2);
    CHECK(recorder.Lines[0] == "[ReportQueue] drained");
    CHECK(recorder.Lines[1] == "[Session] open");
}

TEST_CASE("Logger formats only the lines that will be printed")
{
    VoltMod::Log::SetHandler({});
    const int unhandled = CountedArgument::Formats;
    Logger<BhopManager>().Error("{}", CountedArgument{});
    CHECK(CountedArgument::Formats == unhandled);

    LogRecorder recorder;
    VoltMod::Log::SetMinimumLevel(LogLevel::Error);

    const int before = CountedArgument::Formats;
    Logger<BhopManager> log;
    log.Info("{}", CountedArgument{});
    log.Warn("{}", CountedArgument{});

    CHECK(CountedArgument::Formats == before);
    CHECK(recorder.Lines.empty());

    log.Error("{}", CountedArgument{});
    CHECK(CountedArgument::Formats == before + 1);
    REQUIRE(recorder.Lines.size() == 1);
    CHECK(recorder.Lines[0] == "[BhopManager] counted");
}
