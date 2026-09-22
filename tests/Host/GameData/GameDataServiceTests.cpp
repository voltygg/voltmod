#include "Host/GameData/GameDataService.hpp"
#include "Support/TempPath.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <doctest/doctest.h>
#include <format>
#include <string>
#include <string_view>

using VoltMod::GameDataLocation;
using VoltMod::GameDataSection;
using VoltMod::GameDataService;

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
static void ResolveFile(GameDataService& gameData, std::string_view text)
{
    const VoltModTests::TempDir base("gamedataservice");
    VoltMod::SetBaseDir(base.Path());
    base.Write("gamedata.jsonc", text);

    gameData.Resolve("gamedata.jsonc");
    VoltMod::SetBaseDir({});
}

/** Resolve a minimal gamedata file containing @p sections. */
static void ResolveSections(GameDataService& gameData, std::string_view sections)
{
    std::string text = R"({ "build": { "server": "1", "verified": "2026-09-11" })";
    if (!sections.empty())
    {
        text += std::format(",\n{}", sections);
    }
    text += "\n}";

    ResolveFile(gameData, text);
}

static GameDataLocation Look(GameDataService& gameData, GameDataSection kinds, std::string_view key)
{
    return gameData.Lookup(kinds, key);
}

TEST_CASE("Offsets resolve from this platform's column")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80 },
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 }
  })");

    const GameDataLocation system = Look(gameData, GameDataSection::Offset, "GameEntitySystem");
    CHECK(system.Found);
    CHECK(system.Value == (OnWindows ? 88 : 80));
    CHECK(Look(gameData, GameDataSection::Offset, "CheckTransmitPlayerSlot").Value == 576);
}

TEST_CASE("A key the file lacks is not in gamedata")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })");

    const GameDataLocation missing = Look(gameData, GameDataSection::Offset, "CheckTransmitPlayerSlot");
    CHECK_FALSE(missing.Found);
    CHECK(missing.Value == -1);
    CHECK(missing.Reason == "not in gamedata");
}

TEST_CASE("A missing platform column names the platform")
{
    GameDataService gameData;
    ResolveSections(gameData, std::format(R"("offsets": {{ "GameEntitySystem": {{ "{}": 88 }} }})", Elsewhere));

    const GameDataLocation entry = Look(gameData, GameDataSection::Offset, "GameEntitySystem");
    CHECK_FALSE(entry.Found);
    CHECK(entry.Reason == std::format("no {} offset", Here));
}

TEST_CASE("A key in two sections resolves from neither")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("functions": { "GameEntitySystem": { "windows": "48", "linux": "48" } },
  "offsets": { "GameEntitySystem": { "windows": 88, "linux": 80 } })");

    const GameDataLocation entry = Look(gameData, GameDataSection::Offset, "GameEntitySystem");
    CHECK_FALSE(entry.Found);
    CHECK(entry.Reason == "in both 'functions' and 'offsets'");
}

TEST_CASE("A key asked for from a section it is not in names that section")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("offsets": { "CreateEntityByName": { "windows": 8, "linux": 8 } })");

    const GameDataLocation entry = Look(gameData, GameDataSection::Function, "CreateEntityByName");
    CHECK_FALSE(entry.Found);
    CHECK(entry.Reason == "in 'offsets', which this member does not bind from");
}

TEST_CASE("An empty pattern is refused before any scan")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("functions": { "CreateEntityByName": { "windows": "", "linux": "" } })");

    CHECK(Look(gameData, GameDataSection::Function, "CreateEntityByName").Reason == "empty pattern");
}

TEST_CASE("A negative offset or index is refused")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("vtables": {
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": -1, "linux": -1 }
  },
  "offsets": { "GameEntitySystem": { "windows": -8, "linux": -8 } })");

    CHECK(Look(gameData, GameDataSection::VTable, "CBaseEntity::Teleport").Reason == "index -1 is negative");
    CHECK(Look(gameData, GameDataSection::Offset, "GameEntitySystem").Reason == "offset -8 is negative");
}

/** One entry of each kind that needs a module the test process does not have loaded. */
struct ModuleCase
{
    std::string_view Sections;
    GameDataSection Kinds;
    std::string_view Key;
    std::string_view Reason;
};

TEST_CASE("Anything that needs a module the server has not loaded names that module")
{
    const ModuleCase cases[] = {
        {R"("globals": { "CBaseEntity::EmitSoundFilter": {
      "windows": { "pattern": "48 8B", "rel32At": 3 }, "linux": { "pattern": "48 8B", "rel32At": 3 } } })",
         GameDataSection::Function | GameDataSection::Global, "CBaseEntity::EmitSoundFilter",
         "module 'server' is not loaded"},
        {R"("functions": { "CreateEntityByName": { "module": "engine2", "windows": "48 83", "linux": "48 83" } })",
         GameDataSection::Function, "CreateEntityByName", "module 'engine2' is not loaded"},
        {R"("vtables": { "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": 163, "linux": 162 } })",
         GameDataSection::VTable, "CBaseEntity::Teleport", "module 'server' is not loaded"},
        {R"("offsets": { "CServerSideClient::INetworkMessageProcessingPreFilter": {
      "class": "CServerSideClient", "base": "INetworkMessageProcessingPreFilter", "module": "engine2" } })",
         GameDataSection::Offset, "CServerSideClient::INetworkMessageProcessingPreFilter",
         "module 'engine2' is not loaded"},
    };

    for (const ModuleCase& entry : cases)
    {
        const std::string key(entry.Key);
        CAPTURE(key);
        GameDataService gameData;
        ResolveSections(gameData, entry.Sections);

        const GameDataLocation found = Look(gameData, entry.Kinds, entry.Key);
        CHECK_FALSE(found.Found);
        CHECK(found.Value == -1);
        CHECK(found.Address == nullptr);
        CHECK(found.Reason == entry.Reason);
    }
}

TEST_CASE("A base offset whose entry names no class cannot be looked up")
{
    GameDataService gameData;
    ResolveSections(gameData, R"("offsets": { "GameEntitySystem": { "base": "IGameResourceService" } })");

    CHECK(Look(gameData, GameDataSection::Offset, "GameEntitySystem").Reason ==
          "base 'IGameResourceService' names no class");
}

TEST_CASE("A missing file leaves the host with nothing to serve")
{
    GameDataService gameData;
    gameData.Resolve("voltmod-no-such-gamedata.jsonc");

    CHECK_FALSE(gameData.Ready());
    CHECK_FALSE(Look(gameData, GameDataSection::Offset, "GameEntitySystem").Found);
}

TEST_CASE("Only a file this host can read whole is resolved")
{
    struct FileCase
    {
        std::string_view Sections;
        bool Accepted;
    };

    const FileCase cases[] = {
        {R"("offsets": { "GameEntitySystem": { "windows": 88, "linux": 80, "max": 4096 } })", false},
        {R"("signatures": {})", false},
        {R"("offsets": { "GameEntitySystem": { "windows": "88", "linux": 80 } })", false},
        {R"("vtables": [1, 2])", false},
        {R"("globals": { "CBaseGameSystemFactory::sm_pFirst": { "windows": "48 8B" } })", false},
        {R"("functions": { "CreateEntityByName": 7 })", false},
        // The schema key every gamedata file carries.
        {R"("$schema": "./gamedata.schema.json")", true},
    };

    for (const FileCase& entry : cases)
    {
        const std::string sections(entry.Sections);
        CAPTURE(sections);
        GameDataService gameData;
        ResolveSections(gameData, entry.Sections);
        CHECK(gameData.Ready() == entry.Accepted);
    }
}
