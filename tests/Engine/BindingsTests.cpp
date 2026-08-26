#include "Engine/GameDataFile.hpp"

#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <cstdio>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

using VoltMod::Bindings;
using VoltMod::Capabilities;
using VoltMod::Capability;
using VoltMod::ErrorCode;
using VoltMod::GameData;

// Which column of the fabricated file this build reads.
static constexpr bool OnWindows = VoltMod::HostPlatform == VoltMod::GamePlatform::Windows;

/**
 * A gamedata file on disk, deleted with the test.
 *
 * Bind() is checked against real GameData rather than a hand-filled resolution map because the
 * mapping from key to member is exactly what would rot silently. No game module is loaded in the
 * test process, so every pattern and vtable *table* fails to resolve while every offset and vtable
 * *index* succeeds - which is the split this suite pins.
 */
class TempGameData
{
public:
    explicit TempGameData(std::string_view body)
    {
        _path = std::filesystem::temp_directory_path() /
                std::filesystem::path("voltmod-bindings-" + std::to_string(++_counter) + ".jsonc");
        std::ofstream file(_path);
        file << "{ \"version\": 2, \"build\": { \"game\": \"cs2\", \"verified\": \"2026-08-26\" },\n" << body << "\n}";
    }

    ~TempGameData()
    {
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    TempGameData(const TempGameData&) = delete;
    TempGameData& operator=(const TempGameData&) = delete;

    std::string Path() const { return _path.string(); }

private:
    std::filesystem::path _path;
    static inline int _counter = 0;
};

static constexpr std::string_view FullBody = R"(
  "signatures": {
    "CreateEntityByName": { "windows": { "pattern": "48 83 EC 48" }, "linux": { "pattern": "48 8D 05" } },
    "DispatchSpawn": { "windows": { "pattern": "48 89 5C 24" }, "linux": { "pattern": "48 85 FF" } },
    "CEntityInstance_AcceptInput": { "windows": { "pattern": "48 89 5C 24 ?" }, "linux": { "pattern": "55 48 89 F0" } }
  },
  "vtables": {
    "RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "GiveNamedItem": { "class": "CCSPlayer_ItemServices", "windows": 23, "linux": 24 },
    "RemoveAllItems": { "class": "CCSPlayer_ItemServices", "windows": 27, "linux": 28 },
    "Teleport": { "class": "CCSPlayerPawn", "windows": 163, "linux": 162 }
  },
  "offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80, "align": 8 },
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 },
    "UserCmdPB": { "windows": 16, "linux": 16, "align": 8 },
    "UserCmdNumber": { "windows": 8, "linux": 8, "align": 4 }
  })";

TEST_CASE("Bind refuses an empty gamedata set")
{
    GameData data;
    Capabilities caps;
    Bindings bindings;

    auto bound = bindings.Bind(data, caps);
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().Code == ErrorCode::NotReady);
    CHECK_FALSE(caps.Has(Capability::Entities));
}

TEST_CASE("Bind fills offsets and vtable indices from their gamedata keys")
{
    TempGameData file(FullBody);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Capabilities caps;
    Bindings bindings;
    REQUIRE(bindings.Bind(data, caps).has_value());

    CHECK(bindings.GameEntitySystem.Value() == (OnWindows ? 88 : 80));
    CHECK(bindings.CheckTransmitPlayerSlot.Value() == 576);
    CHECK(bindings.UserCmdPB.Value() == 16);
    CHECK(bindings.UserCmdNumber.Value() == 8);
    CHECK(static_cast<bool>(bindings.GiveNamedItem));
    CHECK(bindings.RemoveAllItems.Index() == (OnWindows ? 27 : 28));
    CHECK(bindings.Teleport.Index() == (OnWindows ? 163 : 162));

    // Offsets and indices need no loaded module, so their capabilities hold.
    CHECK(caps.Has(Capability::Entities));
    CHECK(caps.Has(Capability::Transmit));
    CHECK(caps.Has(Capability::Items));
    CHECK(caps.Has(Capability::Teleport));
}

TEST_CASE("Bind leaves a signature empty and names the module when it cannot be scanned")
{
    TempGameData file(FullBody);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Capabilities caps;
    Bindings bindings;
    REQUIRE(bindings.Bind(data, caps).has_value());

    // No game module is mapped into a unit-test process.
    CHECK_FALSE(static_cast<bool>(bindings.CreateEntityByName));
    CHECK_FALSE(caps.Has(Capability::EntityOps));
    CHECK(std::string(caps.Reason(Capability::EntityOps)).find("CreateEntityByName") != std::string::npos);

    // The RunCommand index is present, but its class vtable is not, so Movement stays off with the
    // table's reason rather than the index's.
    CHECK(static_cast<bool>(bindings.RunCommand));
    CHECK_FALSE(static_cast<bool>(bindings.MovementServices));
    CHECK_FALSE(caps.Has(Capability::Movement));
    CHECK_FALSE(std::string(caps.Reason(Capability::Movement)).empty());
}

TEST_CASE("Bind records a missing key as the capability's reason and leaves the member empty")
{
    // Same document with CheckTransmitPlayerSlot removed.
    TempGameData file(R"(
  "offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80, "align": 8 }
  })");

    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Capabilities caps;
    Bindings bindings;
    REQUIRE(bindings.Bind(data, caps).has_value());

    CHECK_FALSE(static_cast<bool>(bindings.CheckTransmitPlayerSlot));
    CHECK(bindings.CheckTransmitPlayerSlot.Value() == -1);
    CHECK_FALSE(caps.Has(Capability::Transmit));
    CHECK(caps.Reason(Capability::Transmit) == "'CheckTransmitPlayerSlot' is not in gamedata");

    // The one key that is present still binds, and its capability holds.
    CHECK(caps.Has(Capability::Entities));
    CHECK(caps.Reason(Capability::Entities).empty());
}

TEST_CASE("Capabilities summary counts what is on and explains what is not")
{
    Capabilities caps;
    CHECK_FALSE(caps.Has(Capability::Movement));
    CHECK(caps.Reason(Capability::Movement) == "not initialized");

    caps.Set(Capability::Movement, false, "RunCommand vtable unresolved");
    CHECK(caps.Summary().find("Movement: RunCommand vtable unresolved") != std::string::npos);

    caps.Set(Capability::Movement, true);
    CHECK(caps.Has(Capability::Movement));
    CHECK(caps.Reason(Capability::Movement).empty());
    CHECK(caps.Summary().find("1/14 ok") != std::string::npos);
}
