#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <doctest/doctest.h>
#include <optional>
#include <string>

using VoltMod::ErrorCode;
using VoltMod::Player;
using VoltMod::PlayerManager;
using VoltMod::PlayerRef;
using VoltMod::Policy;
using VoltMod::SlotEvents;

static constexpr int64_t AdminId = 76561198000000001LL;
static constexpr int64_t TargetId = 76561198000000002LL;

/** Roster + policy with no engine behind them, the way Runtime wires the pair. */
struct Gate
{
    SlotEvents Slots;
    PlayerManager Players{Slots, nullptr};
    Policy Rules{Players};

    Player& AddAdmin() { return *Players.Add(1, AdminId, "admin", ""); }
    Player& AddTarget() { return *Players.Add(2, TargetId, "target", ""); }
};

TEST_CASE("Authorize fails when the caller is not connected")
{
    Gate gate;
    gate.AddTarget();

    auto result = gate.Rules.Authorize(PlayerRef{.Slot = 1, .SteamId = AdminId}, std::nullopt, "");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::NotFound);
    CHECK(result.error().Key.empty());
}

TEST_CASE("Authorize fails when the caller's slot has changed hands")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    const PlayerRef stale = admin.Ref();
    gate.Players.Add(1, TargetId, "somebody else", "");

    auto result = gate.Rules.Authorize(stale, std::nullopt, "");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::NotFound);
}

TEST_CASE("Authorize fails with a player-facing key when the target is gone")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    Player& target = gate.AddTarget();
    const PlayerRef targetRef = target.Ref();
    gate.Players.Remove(2);

    auto result = gate.Rules.Authorize(admin.Ref(), targetRef, "");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::NotFound);
    CHECK(result.error().Key == "target.noMatch");
}

// The menu case: a row built for one player is pressed after that player left and somebody else
// took the slot. The stored reference must be refused, not resolved to the new occupant - which
// is why ActionDispatcher takes PlayerRef rather than re-deriving one from a slot.
TEST_CASE("Authorize refuses a target whose slot has been taken by somebody else")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    Player& target = gate.AddTarget();
    const PlayerRef stale = target.Ref();

    gate.Players.Remove(2);
    Player* newcomer = gate.Players.Add(2, 76561198000000003LL, "newcomer", "");
    REQUIRE(newcomer != nullptr);

    auto result = gate.Rules.Authorize(admin.Ref(), stale, "");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::NotFound);

    // The slot itself is perfectly usable - it is the *identity* that went stale.
    CHECK(gate.Rules.Authorize(admin.Ref(), newcomer->Ref(), "").has_value());
}

TEST_CASE("An empty permission skips the permission check")
{
    Gate gate;
    Player& admin = gate.AddAdmin();

    bool asked = false;
    gate.Rules.HasPermission = [&](int64_t, std::string_view) {
        asked = true;
        return false;
    };

    auto result = gate.Rules.Authorize(admin.Ref(), std::nullopt, "");
    REQUIRE(result);
    CHECK(!asked);
    CHECK(&result->Caller == &admin);
    CHECK(result->Target == nullptr);
}

TEST_CASE("An unset HasPermission denies every permission-gated action")
{
    Gate gate;
    Player& admin = gate.AddAdmin();

    // The warning is logged once, but every attempt is denied.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        auto result = gate.Rules.Authorize(admin.Ref(), std::nullopt, "ban");
        REQUIRE(!result);
        CHECK(result.error().Code == ErrorCode::Denied);
        CHECK(result.error().Key == "cmd.noPermission");
    }
}

TEST_CASE("A refused permission is denied with the reserved key")
{
    Gate gate;
    Player& admin = gate.AddAdmin();

    std::string asked;
    gate.Rules.HasPermission = [&](int64_t steamId, std::string_view permission) {
        asked = permission;
        return steamId == AdminId && permission == "kick";
    };

    auto granted = gate.Rules.Authorize(admin.Ref(), std::nullopt, "kick");
    CHECK(granted);

    auto denied = gate.Rules.Authorize(admin.Ref(), std::nullopt, "ban");
    REQUIRE(!denied);
    CHECK(asked == "ban");
    CHECK(denied.error().Code == ErrorCode::Denied);
    CHECK(denied.error().Key == "cmd.noPermission");
}

TEST_CASE("A target the caller may not act on is immune")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    Player& target = gate.AddTarget();

    gate.Rules.CanTarget = [](const Player&, const Player&) { return false; };

    auto result = gate.Rules.Authorize(admin.Ref(), target.Ref(), "");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::Immune);
    CHECK(result.error().Key == "target.immune");
}

TEST_CASE("An authorized pair carries both players")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    Player& target = gate.AddTarget();

    gate.Rules.HasPermission = [](int64_t, std::string_view) { return true; };
    gate.Rules.CanTarget = [](const Player& caller, const Player& other) {
        return caller.SteamId() == AdminId && other.SteamId() == TargetId;
    };

    auto result = gate.Rules.Authorize(admin.Ref(), target.Ref(), "slay");
    REQUIRE(result);
    CHECK(&result->Caller == &admin);
    CHECK(result->Target == &target);
}

TEST_CASE("Targeting yourself never reaches CanTarget")
{
    Gate gate;
    Player& admin = gate.AddAdmin();

    bool asked = false;
    gate.Rules.CanTarget = [&](const Player&, const Player&) {
        asked = true;
        return false;
    };

    auto result = gate.Rules.Authorize(admin.Ref(), admin.Ref(), "");
    REQUIRE(result);
    CHECK(!asked);
    CHECK(result->Target == &admin);
}

TEST_CASE("Targeting yourself still needs the permission")
{
    Gate gate;
    Player& admin = gate.AddAdmin();

    gate.Rules.HasPermission = [](int64_t, std::string_view) { return false; };

    auto result = gate.Rules.Authorize(admin.Ref(), admin.Ref(), "slay");
    REQUIRE(!result);
    CHECK(result.error().Code == ErrorCode::Denied);
}

TEST_CASE("An unset CanTarget allows every pair")
{
    Gate gate;
    Player& admin = gate.AddAdmin();
    Player& target = gate.AddTarget();

    auto result = gate.Rules.Authorize(admin.Ref(), target.Ref(), "");
    REQUIRE(result);
    CHECK(result->Target == &target);
}
