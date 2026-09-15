#include "Engine/GameData/GameDataFile.hpp"
#include "Support/TempPath.hpp"

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/GameData/GameData.hpp>
#include <algorithm>
#include <cstddef>
#include <doctest/doctest.h>
#include <format>
#include <string>
#include <string_view>

using VoltMod::Bindings;
using VoltMod::ErrorCode;
using VoltMod::GameData;

// Select the host platform column in the fabricated file.
static constexpr bool OnWindows = VoltMod::HostPlatform == VoltMod::GamePlatform::Windows;

/** Temporary gamedata file for key-to-member tests; each case supplies only its entries. */
class TempGameData
{
public:
    explicit TempGameData(std::string_view body)
        : _file(std::format("{{ \"build\": {{ \"game\": \"cs2\", \"verified\": \"2026-08-26\" }},\n"
                            "{}\n}}",
                            body),
                "bindings", ".jsonc")
    {}

    std::string Path() const { return _file.Path(); }

private:
    VoltModTests::TempFile _file;
};

/** How many of @p bindings' failures are about @p key: `'key' ...` or `key: ...`. */
static size_t FailuresFor(const Bindings& bindings, std::string_view key)
{
    const std::string quoted = std::format("'{}'", key);
    const std::string prefixed = std::format("{}: ", key);
    return static_cast<size_t>(std::ranges::count_if(bindings.Failures, [&](const std::string& failure) {
        return failure.starts_with(quoted) || failure.starts_with(prefixed);
    }));
}

static constexpr std::string_view FullBody = R"(
  "signatures": {
    "CreateEntityByName": { "windows": { "pattern": "48 83 EC 48" }, "linux": { "pattern": "48 8D 05" } },
    "DispatchSpawn": { "windows": { "pattern": "48 89 5C 24" }, "linux": { "pattern": "48 85 FF" } },
    "CEntityInstance::AcceptInput": { "windows": { "pattern": "48 89 5C 24 ?" }, "linux": { "pattern": "55 48 89 F0" } }
  },
  "vtables": {
    "CPlayer_MovementServices::RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "CCSPlayer_ItemServices::GiveNamedItem": { "class": "CCSPlayer_ItemServices", "windows": 23, "linux": 24 },
    "CCSPlayer_ItemServices::RemoveAllItems": { "class": "CCSPlayer_ItemServices", "windows": 27, "linux": 28 },
    "CBaseEntity::Teleport": { "class": "CCSPlayerPawn", "windows": 163, "linux": 162 }
  },
  "offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80, "align": 8 },
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 },
    "CUserCmd::CSGOUserCmdPB": { "windows": 16, "linux": 16, "align": 8 },
    "CUserCmdBase::cmdNum": { "windows": 8, "linux": 8, "align": 4 }
  })";

static constexpr std::string_view OneOffset = R"(
  "offsets": {
    "GameEntitySystem": { "windows": 88, "linux": 80, "align": 8 }
  })";

TEST_CASE("Bind refuses an empty gamedata set")
{
    GameData data;
    Bindings bindings;

    auto bound = bindings.Bind(data);
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().Code == ErrorCode::NotReady);
}

TEST_CASE("Bind fills offsets and vtable indices from their gamedata keys")
{
    TempGameData file(FullBody);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Bindings bindings;
    CHECK_FALSE(bindings.Bind(data).has_value());

    CHECK(bindings.GameEntitySystem.Value() == (OnWindows ? 88 : 80));
    CHECK(bindings.VisibilityRecipientSlot.Value() == 576);
    CHECK(bindings.UserCmdProto.Value() == 16);
    CHECK(bindings.UserCmdNumber.Value() == 8);
    CHECK(bindings.GiveNamedItem.Index() == (OnWindows ? 23 : 24));
    CHECK(bindings.RemoveAllItems.Index() == (OnWindows ? 27 : 28));
    CHECK(bindings.Teleport.Index() == (OnWindows ? 163 : 162));

    CHECK(FailuresFor(bindings, "GameEntitySystem") == 0);
    CHECK(FailuresFor(bindings, "CheckTransmitPlayerSlot") == 0);
}

TEST_CASE("Bind leaves a signature empty and names the module when it cannot be scanned")
{
    TempGameData file(FullBody);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Bindings bindings;
    CHECK_FALSE(bindings.Bind(data).has_value());

    CHECK_FALSE(static_cast<bool>(bindings.CreateEntityByName));
    CHECK(FailuresFor(bindings, "CreateEntityByName") == 1);

    CHECK(bindings.RunCommand.Index() == (OnWindows ? 25 : 26));
    CHECK(bindings.RunCommand.Table() == nullptr);
    CHECK_FALSE(static_cast<bool>(bindings.RunCommand));
    CHECK(FailuresFor(bindings, "CPlayer_MovementServices::RunCommand") == 1);
}

TEST_CASE("Bind names a key the file lacks and leaves its member empty")
{
    TempGameData file(OneOffset);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Bindings bindings;
    const auto bound = bindings.Bind(data);
    REQUIRE_FALSE(bound.has_value());
    CHECK(bound.error().Detail.find("'CheckTransmitPlayerSlot' is not in gamedata") != std::string::npos);

    CHECK_FALSE(static_cast<bool>(bindings.VisibilityRecipientSlot));
    CHECK(bindings.VisibilityRecipientSlot.Value() == -1);
    CHECK(FailuresFor(bindings, "CheckTransmitPlayerSlot") == 1);
    CHECK(FailuresFor(bindings, "GameEntitySystem") == 0);
}

TEST_CASE("Bind names each failing key once, however many services use it")
{
    TempGameData file(OneOffset);
    GameData data;
    REQUIRE(data.Load(file.Path()).has_value());

    Bindings bindings;
    CHECK_FALSE(bindings.Bind(data).has_value());

    CHECK(FailuresFor(bindings, "CServerSideClientBase::m_nClientSlot") == 1);
    CHECK(FailuresFor(bindings, "CUserCmdBase::cmdNum") == 1);
}
