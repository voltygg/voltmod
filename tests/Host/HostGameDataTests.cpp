#include "Host/HostGameData.hpp"
#include "Host/PluginHost.hpp"
#include "Support/TempPath.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <doctest/doctest.h>
#include <format>
#include <string>
#include <string_view>

using VoltMod::Borrowed;
using VoltMod::GameDataEntry;
using VoltMod::GameDataKind;
using VoltMod::HostGameData;
using VoltMod::Text;

#ifdef _WIN32
static constexpr std::string_view Here = "windows";
static constexpr std::string_view Elsewhere = "linux";
#else
static constexpr std::string_view Here = "linux";
static constexpr std::string_view Elsewhere = "windows";
#endif

static constexpr bool OnWindows = Here == "windows";

/** Resolve @p text as the gamedata file. The record the host writes lands in a temporary base
 *  directory that goes away with the test. */
static void ResolveFile(HostGameData& gameData, std::string_view text)
{
    const VoltModTests::TempDir base("hostgamedata");
    VoltMod::SetBaseDir(base.Path());
    base.Write("gamedata.jsonc", text);

    gameData.Resolve("gamedata.jsonc");
    VoltMod::SetBaseDir({});
}

/** Resolve a minimal gamedata file containing @p sections. */
static void ResolveSections(HostGameData& gameData, std::string_view sections)
{
    std::string text = R"({ "build": { "server": "1", "verified": "2026-09-11" })";
    if (!sections.empty())
        text += std::format(",\n{}", sections);
    text += "\n}";

    ResolveFile(gameData, text);
}

static GameDataEntry Look(HostGameData& gameData, GameDataKind kinds, std::string_view key)
{
    return gameData.Lookup(kinds, Borrowed(key));
}

TEST_CASE("Offsets resolve from this platform's column")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80 },
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 }
  })");

    const GameDataEntry system = Look(gameData, GameDataKind::Offset, "GameEntitySystem");
    CHECK(system.Found);
    CHECK(system.Value == (OnWindows ? 88 : 80));
    CHECK(Look(gameData, GameDataKind::Offset, "CheckTransmitPlayerSlot").Value == 576);
}

TEST_CASE("A key the file lacks is not in gamedata")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })");

    const GameDataEntry missing = Look(gameData, GameDataKind::Offset, "CheckTransmitPlayerSlot");
    CHECK_FALSE(missing.Found);
    CHECK(missing.Value == -1);
    CHECK(Text(missing.Reason) == "not in gamedata");
}

TEST_CASE("A missing platform column names the platform")
{
    HostGameData gameData;
    ResolveSections(gameData, std::format(R"("offsets": {{ "GameEntitySystem": {{ "{}": 88 }} }})", Elsewhere));

    const GameDataEntry entry = Look(gameData, GameDataKind::Offset, "GameEntitySystem");
    CHECK_FALSE(entry.Found);
    CHECK(Text(entry.Reason) == std::format("no {} offset", Here));
}

TEST_CASE("A key in two sections resolves from neither")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("functions": { "GameEntitySystem": { "windows": "48", "linux": "48" } },
  "offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })");

    const GameDataEntry entry = Look(gameData, GameDataKind::Offset, "GameEntitySystem");
    CHECK_FALSE(entry.Found);
    CHECK(Text(entry.Reason) == "in both 'functions' and 'offsets'");
}

TEST_CASE("A key asked for from a section it is not in names that section")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("offsets": { "CreateEntityByName": { "windows": 8, "linux": 8 } })");

    const GameDataEntry entry = Look(gameData, GameDataKind::Function, "CreateEntityByName");
    CHECK_FALSE(entry.Found);
    CHECK(Text(entry.Reason) == "in 'offsets', which this member does not bind from");
}

TEST_CASE("An address resolves from a function or a global")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("globals": {
    "CBaseEntity::EmitSoundFilter": { "windows": { "pattern": "48 8B", "rel32At": 3 }, "linux": { "pattern": "48 8B", "rel32At": 3 } }
  })");

    const GameDataEntry entry =
        Look(gameData, GameDataKind::Function | GameDataKind::Global, "CBaseEntity::EmitSoundFilter");
    CHECK_FALSE(entry.Found);
    CHECK(Text(entry.Reason) == "module 'server' is not loaded");
}

TEST_CASE("A function in a module that is not loaded names the module")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("functions": {
    "CreateEntityByName": { "module": "engine2", "windows": "48 83", "linux": "48 83" }
  })");

    CHECK(Text(Look(gameData, GameDataKind::Function, "CreateEntityByName").Reason) ==
          "module 'engine2' is not loaded");
}

TEST_CASE("An empty pattern is refused before any scan")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("functions": { "CreateEntityByName": { "windows": "", "linux": "" } })");

    CHECK(Text(Look(gameData, GameDataKind::Function, "CreateEntityByName").Reason) == "empty pattern");
}

TEST_CASE("A vtable slot resolves only with its class table")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("vtables": {
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": 163, "linux": 162 }
  })");

    const GameDataEntry entry = Look(gameData, GameDataKind::VTable, "CBaseEntity::Teleport");
    CHECK_FALSE(entry.Found);
    CHECK(entry.Value == -1);
    CHECK(entry.Address == nullptr);
    CHECK(Text(entry.Reason) == "module 'server' is not loaded");
}

TEST_CASE("A negative offset or index is refused")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("vtables": {
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": -1, "linux": -1 }
  },
  "offsets": { "GameEntitySystem": { "windows": -8, "linux": -8 } })");

    CHECK(Text(Look(gameData, GameDataKind::VTable, "CBaseEntity::Teleport").Reason) == "index -1 is negative");
    CHECK(Text(Look(gameData, GameDataKind::Offset, "GameEntitySystem").Reason) == "offset -8 is negative");
}

TEST_CASE("A base offset is looked up in its class's module, and needs a class")
{
    HostGameData gameData;
    ResolveSections(gameData, R"("offsets": {
    "CServerSideClient::INetworkMessageProcessingPreFilter": { "class": "CServerSideClient", "base": "INetworkMessageProcessingPreFilter", "module": "engine2" },
    "GameEntitySystem": { "base": "IGameResourceService" }
  })");

    CHECK(Text(Look(gameData, GameDataKind::Offset, "CServerSideClient::INetworkMessageProcessingPreFilter").Reason) ==
          "module 'engine2' is not loaded");
    CHECK(Text(Look(gameData, GameDataKind::Offset, "GameEntitySystem").Reason) ==
          "base 'IGameResourceService' names no class");
}

TEST_CASE("A missing file leaves the host with nothing to serve")
{
    HostGameData gameData;
    gameData.Resolve("voltmod-no-such-gamedata.jsonc");

    CHECK_FALSE(gameData.Ready());
    CHECK_FALSE(Look(gameData, GameDataKind::Offset, "GameEntitySystem").Found);
}

TEST_CASE("A malformed file is refused before anything resolves")
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
        HostGameData gameData;
        ResolveSections(gameData, sections);
        CHECK_FALSE(gameData.Ready());
    }
}

TEST_CASE("The schema key every gamedata file carries is accepted")
{
    HostGameData gameData;
    ResolveFile(gameData,
                R"({ "$schema": "./gamedata.schema.json", "build": { "server": "1", "verified": "2026-09-11" } })");

    CHECK(gameData.Ready());
}
