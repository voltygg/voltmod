#include "Support/TempPath.hpp"

#include <VoltMod/App/Config/Options.hpp>
#include <VoltMod/Core/Result.hpp>
#include <algorithm>
#include <doctest/doctest.h>
#include <string>
#include <vector>

// At file scope because Glaze reflects their member names.

struct LimitSettings
{
    int maxPlayers = 10;
    std::string tag = "default";
};

struct OptionsSample
{
    LimitSettings limits;
    std::vector<std::string> durations = {"5m", "1h"};
};

/** What a plugin publishes: the checked settings plus the values derived from them. */
struct OptionsSnapshot
{
    OptionsSample Values;
    std::vector<std::string> LongDurations;
};

static OptionsSnapshot BuildOptionsSnapshot(OptionsSample raw)
{
    OptionsSnapshot snapshot{.Values = std::move(raw)};
    snapshot.Values.limits.maxPlayers = std::clamp(snapshot.Values.limits.maxPlayers, 1, 64);
    for (const std::string& duration : snapshot.Values.durations)
        if (duration.ends_with("h"))
            snapshot.LongDurations.push_back(duration);
    return snapshot;
}

using VoltModTests::TempFile;

TEST_CASE("Options publishes the parsed settings")
{
    const TempFile file(R"({"limits":{"maxPlayers":24,"tag":"eu-1"}})", "options", ".jsonc");

    VoltMod::Options<OptionsSample> options;
    REQUIRE(options.Load(file.Path()).has_value());
    CHECK(options.Get().limits.maxPlayers == 24);
    CHECK(options.Get().limits.tag == "eu-1");
    // A key the file omits keeps its C++ initializer.
    CHECK(options.Get().durations == std::vector<std::string>{"5m", "1h"});
}

TEST_CASE("Options reports a missing file and leaves the defaults in place")
{
    VoltMod::Options<OptionsSample> options;
    auto loaded = options.Load("definitely/not/here.jsonc");
    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().Code == VoltMod::ErrorCode::NotFound);
    CHECK(options.Get().limits.maxPlayers == 10);
}

TEST_CASE("A malformed reload leaves the previous snapshot intact")
{
    const TempFile good(R"({"limits":{"maxPlayers":24,"tag":"eu-1"}})", "options", ".jsonc");
    const TempFile broken(R"({"limits":{"maxPlayers":)", "options", ".jsonc");

    VoltMod::Options<OptionsSample> options;
    REQUIRE(options.Load(good.Path()).has_value());

    auto reloaded = options.Load(broken.Path());
    REQUIRE_FALSE(reloaded.has_value());
    CHECK(reloaded.error().Code == VoltMod::ErrorCode::Invalid);
    CHECK(options.Get().limits.maxPlayers == 24);
    CHECK(options.Get().limits.tag == "eu-1");
}

TEST_CASE("Options runs the builder and publishes what it returns")
{
    const TempFile file(R"({"limits":{"maxPlayers":900},"durations":["30s","2h","3h"]})", "options", ".jsonc");

    VoltMod::Options<OptionsSample, OptionsSnapshot> options{&BuildOptionsSnapshot};
    REQUIRE(options.Load(file.Path()).has_value());
    CHECK(options.Get().Values.limits.maxPlayers == 64);
    CHECK(options.Get().LongDurations == std::vector<std::string>{"2h", "3h"});
}

TEST_CASE("A derived value survives a failed reload with the settings it came from")
{
    const TempFile good(R"({"durations":["2h"]})", "options", ".jsonc");
    const TempFile broken("not json at all", "options", ".jsonc");

    VoltMod::Options<OptionsSample, OptionsSnapshot> options{&BuildOptionsSnapshot};
    REQUIRE(options.Load(good.Path()).has_value());
    REQUIRE_FALSE(options.Load(broken.Path()).has_value());

    CHECK(options.Get().Values.durations == std::vector<std::string>{"2h"});
    CHECK(options.Get().LongDurations == std::vector<std::string>{"2h"});
}
