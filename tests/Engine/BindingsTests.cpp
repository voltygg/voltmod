#include "Support/TempPath.hpp"

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <algorithm>
#include <cstddef>
#include <doctest/doctest.h>
#include <format>
#include <initializer_list>
#include <string>
#include <string_view>

using VoltMod::Bindings;
using VoltMod::ErrorCode;

#ifdef _WIN32
static constexpr std::string_view Here = "windows";
static constexpr std::string_view Elsewhere = "linux";
#else
static constexpr std::string_view Here = "linux";
static constexpr std::string_view Elsewhere = "windows";
#endif

static constexpr bool OnWindows = Here == "windows";

/** Load a minimal gamedata file containing @p sections. */
static VoltMod::Status LoadSections(Bindings& bindings, std::string_view sections)
{
    std::string text = R"({ "build": { "server": "1", "verified": "2026-09-11" })";
    if (!sections.empty())
        text += std::format(",\n{}", sections);
    text += "\n}";

    VoltModTests::TempFile file(text, "bindings", ".jsonc");
    return bindings.Load(file.Path());
}

static bool HasFailure(const Bindings& bindings, std::string_view failure)
{
    return std::ranges::any_of(bindings.Failures, [&](const std::string& each) { return each == failure; });
}

/** Count failures for @p key. */
static size_t FailuresFor(const Bindings& bindings, std::string_view key)
{
    const std::string prefix = std::format("{}: ", key);
    return static_cast<size_t>(std::ranges::count_if(
        bindings.Failures, [&](const std::string& failure) { return failure.starts_with(prefix); }));
}

TEST_CASE("Load binds offsets from this platform's column")
{
    Bindings bindings;
    const auto loaded = LoadSections(bindings, R"("offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80 },
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 },
    "CUserCmd::CSGOUserCmdPB": { "windows": 16, "linux": 16 },
    "CUserCmdBase::cmdNum": { "windows": 8, "linux": 8 }
  })");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().Code == ErrorCode::Engine);

    CHECK(bindings.GameEntitySystem.Value() == (OnWindows ? 88 : 80));
    CHECK(bindings.VisibilityRecipientSlot.Value() == 576);
    CHECK(bindings.UserCmdProto.Value() == 16);
    CHECK(bindings.UserCmdNumber.Value() == 8);
    CHECK(FailuresFor(bindings, "GameEntitySystem") == 0);
    CHECK(FailuresFor(bindings, "CheckTransmitPlayerSlot") == 0);
}

TEST_CASE("Load names a key the file lacks and leaves its member unbound")
{
    Bindings bindings;
    const auto loaded = LoadSections(bindings, R"("offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().Detail.find("CheckTransmitPlayerSlot: not in gamedata") != std::string::npos);
    CHECK_FALSE(static_cast<bool>(bindings.VisibilityRecipientSlot));
    CHECK(FailuresFor(bindings, "CheckTransmitPlayerSlot") == 1);
    CHECK(FailuresFor(bindings, "CServerSideClientBase::m_nClientSlot") == 1);
}

TEST_CASE("A missing platform column fails only an entry something binds")
{
    Bindings bindings;
    const std::string sections = std::format(R"("offsets": {{
    "GameEntitySystem": {{ "{0}": 88 }},
    "NothingBindsThis": {{ "{0}": 8 }}
  }})",
                                             Elsewhere);
    CHECK_FALSE(LoadSections(bindings, sections).has_value());

    CHECK_FALSE(static_cast<bool>(bindings.GameEntitySystem));
    CHECK(HasFailure(bindings, std::format("GameEntitySystem: no {} offset", Here)));
    CHECK(FailuresFor(bindings, "NothingBindsThis") == 0);
}

TEST_CASE("A key in two sections binds from neither")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("functions": { "GameEntitySystem": { "windows": "48", "linux": "48" } },
  "offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })")
                    .has_value());

    CHECK_FALSE(static_cast<bool>(bindings.GameEntitySystem));
    CHECK(HasFailure(bindings, "GameEntitySystem: in both 'functions' and 'offsets'"));
}

TEST_CASE("A key in a section its member does not bind from is named")
{
    Bindings bindings;
    CHECK_FALSE(
        LoadSections(bindings, R"("offsets": { "CreateEntityByName": { "windows": 8, "linux": 8 } })").has_value());

    CHECK_FALSE(static_cast<bool>(bindings.CreateEntityByName));
    CHECK(HasFailure(bindings, "CreateEntityByName: in 'offsets', which this member does not bind from"));
}

TEST_CASE("An address binds from a function or a global")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("globals": {
    "CBaseEntity::EmitSoundFilter": { "windows": { "pattern": "48 8B", "rel32At": 3 }, "linux": { "pattern": "48 8B", "rel32At": 3 } }
  })")
                    .has_value());

    CHECK(HasFailure(bindings, "CBaseEntity::EmitSoundFilter: module 'server' is not loaded"));
}

TEST_CASE("A function in a module that is not loaded names the module")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("functions": {
    "CreateEntityByName": { "module": "engine2", "windows": "48 83", "linux": "48 83" }
  })")
                    .has_value());

    CHECK_FALSE(static_cast<bool>(bindings.CreateEntityByName));
    CHECK(HasFailure(bindings, "CreateEntityByName: module 'engine2' is not loaded"));
}

TEST_CASE("An empty pattern is refused before any scan")
{
    Bindings bindings;
    CHECK_FALSE(
        LoadSections(bindings, R"("functions": { "CreateEntityByName": { "windows": "", "linux": "" } })").has_value());

    CHECK(HasFailure(bindings, "CreateEntityByName: empty pattern"));
}

TEST_CASE("A vtable slot binds only with its class table")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("vtables": {
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": 163, "linux": 162 }
  })")
                    .has_value());

    CHECK_FALSE(static_cast<bool>(bindings.Teleport));
    CHECK(bindings.Teleport.Index() == -1);
    CHECK(HasFailure(bindings, "CBaseEntity::Teleport: module 'server' is not loaded"));
}

TEST_CASE("A negative offset or index is refused")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("vtables": {
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": -1, "linux": -1 }
  },
  "offsets": { "GameEntitySystem": { "windows": -8, "linux": -8 } })")
                    .has_value());

    CHECK(HasFailure(bindings, "CBaseEntity::Teleport: index -1 is negative"));
    CHECK(HasFailure(bindings, "GameEntitySystem: offset -8 is negative"));
    CHECK_FALSE(static_cast<bool>(bindings.GameEntitySystem));
}

TEST_CASE("A base offset is looked up in its class's module, and needs a class")
{
    Bindings bindings;
    CHECK_FALSE(LoadSections(bindings, R"("offsets": {
    "CServerSideClient::INetworkMessageProcessingPreFilter": { "class": "CServerSideClient", "base": "INetworkMessageProcessingPreFilter", "module": "engine2" },
    "GameEntitySystem": { "base": "IGameResourceService" }
  })")
                    .has_value());

    CHECK(
        HasFailure(bindings, "CServerSideClient::INetworkMessageProcessingPreFilter: module 'engine2' is not loaded"));
    CHECK(HasFailure(bindings, "GameEntitySystem: base 'IGameResourceService' names no class"));
}

TEST_CASE("A missing file is NotFound and binds nothing")
{
    Bindings bindings;
    const auto loaded = bindings.Load("voltmod-no-such-gamedata.jsonc");

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().Code == ErrorCode::NotFound);
    CHECK(bindings.Failures.empty());
}

TEST_CASE("A malformed file is refused before anything binds")
{
    for (std::string_view sections : {
             R"("offsets": { "GameEntitySystem": { "windows": 88, "linux": 80, "max": 4096 } })",
             R"("signatures": {})",
             R"("offsets": { "GameEntitySystem": { "windows": "88", "linux": 80 } })",
             R"("vtables": [1, 2])",
             R"("globals": { "CBaseGameSystemFactory::sm_pFirst": { "windows": "48 8B" } })",
             R"("functions": { "CreateEntityByName": 7 })",
         })
    {
        Bindings bindings;
        const auto loaded = LoadSections(bindings, sections);

        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().Code == ErrorCode::Invalid);
        CHECK(bindings.Failures.empty());
    }
}

TEST_CASE("The schema key every gamedata file carries is accepted")
{
    VoltModTests::TempFile file(
        R"({ "$schema": "./gamedata.schema.json", "build": { "server": "1", "verified": "2026-09-11" } })", "bindings",
        ".jsonc");
    Bindings bindings;
    const auto loaded = bindings.Load(file.Path());

    REQUIRE_FALSE(loaded.has_value());
    CHECK(loaded.error().Code == ErrorCode::Engine);
}
