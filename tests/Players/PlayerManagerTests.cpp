#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <doctest/doctest.h>

using VoltMod::PlayerManager;
using VoltMod::SlotEvents;

static constexpr int64_t SteamA = 76561198000000001LL;
static constexpr int64_t SteamB = 76561198000000002LL;

TEST_CASE("A player is reachable by slot and by SteamID")
{
    SlotEvents slots;
    PlayerManager players(slots);

    auto* added = players.AddPlayer(3, SteamA, "alpha", "1.2.3.4");

    CHECK(players.GetPlayerBySlot(3) == added);
    CHECK(players.GetPlayerBySteamId(SteamA) == added);
}

TEST_CASE("Re-occupying a slot drops the previous occupant from the SteamID index")
{
    SlotEvents slots;
    PlayerManager players(slots);

    players.AddPlayer(3, SteamA, "alpha", "1.2.3.4");
    auto* second = players.AddPlayer(3, SteamB, "bravo", "5.6.7.8");

    // Reusing a slot must remove the previous occupant's SteamID index.
    CHECK(players.GetPlayerBySteamId(SteamA) == nullptr);
    CHECK(players.GetPlayerBySteamId(SteamB) == second);
    CHECK(players.GetPlayerBySlot(3) == second);
}

TEST_CASE("Removing a player clears both indexes")
{
    SlotEvents slots;
    PlayerManager players(slots);

    players.AddPlayer(1, SteamA, "alpha", "1.2.3.4");
    players.RemovePlayer(1);

    CHECK(players.GetPlayerBySlot(1) == nullptr);
    CHECK(players.GetPlayerBySteamId(SteamA) == nullptr);
}

TEST_CASE("A reconnect into another slot survives the old slot being removed")
{
    SlotEvents slots;
    PlayerManager players(slots);

    players.AddPlayer(1, SteamA, "alpha", "1.2.3.4");
    auto* rejoined = players.AddPlayer(2, SteamA, "alpha", "1.2.3.4");
    players.RemovePlayer(1);

    CHECK(players.GetPlayerBySteamId(SteamA) == rejoined);
}

TEST_CASE("Bots are not indexed by SteamID")
{
    SlotEvents slots;
    PlayerManager players(slots);

    auto* first = players.AddPlayer(4, 0, "Bot Sam", "");
    auto* second = players.AddPlayer(5, 0, "Bot Kim", "");

    CHECK(first->IsBot());
    CHECK(players.GetPlayerBySteamId(0) == nullptr);
    CHECK(players.GetPlayerBySlot(4) == first);
    CHECK(players.GetPlayerBySlot(5) == second);
}

TEST_CASE("Removing one bot leaves the others reachable by slot")
{
    SlotEvents slots;
    PlayerManager players(slots);

    players.AddPlayer(4, 0, "Bot Sam", "");
    auto* keep = players.AddPlayer(5, 0, "Bot Kim", "");
    players.RemovePlayer(4);

    CHECK(players.GetPlayerBySlot(4) == nullptr);
    CHECK(players.GetPlayerBySlot(5) == keep);
}

TEST_CASE("A slot change fires for both the departing and arriving occupant")
{
    SlotEvents slots;
    PlayerManager players(slots);

    int raised = 0;
    auto sub = slots.Changed += [&](int) { ++raised; };

    players.AddPlayer(6, SteamA, "alpha", "1.2.3.4");
    players.AddPlayer(6, SteamB, "bravo", "5.6.7.8");
    players.RemovePlayer(6);

    CHECK(raised == 3);
}
