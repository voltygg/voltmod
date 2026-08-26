#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::Player;
using VoltMod::PlayerManager;
using VoltMod::PlayerRef;
using VoltMod::SlotEvents;

static constexpr int64_t SteamA = 76561198000000001LL;
static constexpr int64_t SteamB = 76561198000000002LL;

/** A roster with no engine behind it: Player's engine-facing accessors are inert. */
static PlayerManager MakeManager(SlotEvents& slots)
{
    return PlayerManager{slots, nullptr};
}

TEST_CASE("A player is reachable by slot and by SteamID")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    auto* added = players.Add(3, SteamA, "alpha", "1.2.3.4");

    CHECK(players.Get(3) == added);
    CHECK(players.BySteamId(SteamA) == added);
}

TEST_CASE("Re-occupying a slot drops the previous occupant from the SteamID index")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(3, SteamA, "alpha", "1.2.3.4");
    auto* second = players.Add(3, SteamB, "bravo", "5.6.7.8");

    // Reusing a slot must remove the previous occupant's SteamID index.
    CHECK(players.BySteamId(SteamA) == nullptr);
    CHECK(players.BySteamId(SteamB) == second);
    CHECK(players.Get(3) == second);
}

TEST_CASE("Removing a player clears both indexes")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(1, SteamA, "alpha", "1.2.3.4");
    players.Remove(1);

    CHECK(players.Get(1) == nullptr);
    CHECK(players.BySteamId(SteamA) == nullptr);
}

TEST_CASE("A reconnect into another slot survives the old slot being removed")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(1, SteamA, "alpha", "1.2.3.4");
    auto* rejoined = players.Add(2, SteamA, "alpha", "1.2.3.4");
    players.Remove(1);

    CHECK(players.BySteamId(SteamA) == rejoined);
}

TEST_CASE("Bots are not indexed by SteamID")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    auto* first = players.Add(4, 0, "Bot Sam", "");
    auto* second = players.Add(5, 0, "Bot Kim", "");

    CHECK(first->IsBot());
    CHECK(players.BySteamId(0) == nullptr);
    CHECK(players.Get(4) == first);
    CHECK(players.Get(5) == second);
}

TEST_CASE("Removing one bot leaves the others reachable by slot")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(4, 0, "Bot Sam", "");
    auto* keep = players.Add(5, 0, "Bot Kim", "");
    players.Remove(4);

    CHECK(players.Get(4) == nullptr);
    CHECK(players.Get(5) == keep);
}

TEST_CASE("A slot change fires for both the departing and arriving occupant")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    int raised = 0;
    auto sub = slots.Changed += [&](int) { ++raised; };

    players.Add(6, SteamA, "alpha", "1.2.3.4");
    players.Add(6, SteamB, "bravo", "5.6.7.8");
    players.Remove(6);

    CHECK(raised == 3);
}

TEST_CASE("A ref resolves to its own player and to nobody once the slot changes hands")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    auto* first = players.Add(2, SteamA, "alpha", "1.2.3.4");
    const PlayerRef ref = first->Ref();
    CHECK(players.Get(ref) == first);

    players.Add(2, SteamB, "bravo", "5.6.7.8");

    // The slot is occupied again, so a bare slot lookup would hand back the wrong player.
    CHECK(players.Get(2) != nullptr);
    CHECK(players.Get(ref) == nullptr);
}

TEST_CASE("A ref for an empty slot resolves to nobody")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    CHECK(players.Get(PlayerRef{.Slot = 7, .SteamId = SteamA}) == nullptr);
    CHECK(players.RefFor(7) == PlayerRef{});
    CHECK(!players.RefFor(7));
}

TEST_CASE("RefFor names the current occupant")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    auto* added = players.Add(9, SteamA, "alpha", "1.2.3.4");
    CHECK(players.RefFor(9) == added->Ref());
    CHECK(players.Get(players.RefFor(9)) == added);
}

TEST_CASE("All returns every player in slot order")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(5, SteamB, "bravo", "");
    players.Add(1, SteamA, "alpha", "");
    players.Add(3, 0, "Bot Sam", "");

    std::vector<int> seen;
    for (const Player* player : players.All())
        seen.push_back(player->Slot());

    CHECK(seen == std::vector<int>{1, 3, 5});
}

TEST_CASE("All follows the roster across a removal")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(1, SteamA, "alpha", "");
    players.Add(2, SteamB, "bravo", "");
    CHECK(players.All().size() == 2);

    players.Remove(1);
    REQUIRE(players.All().size() == 1);
    CHECK(players.All().front()->SteamId() == SteamB);

    players.Clear();
    CHECK(players.All().empty());
}

TEST_CASE("Connected fires after the player is in the roster")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    Player* seen = nullptr;
    bool reachable = false;
    auto sub = players.Connected += [&](Player& player) {
        seen = &player;
        reachable = players.Get(player.Slot()) == &player;
    };

    auto* added = players.Add(4, SteamA, "alpha", "");
    CHECK(seen == added);
    CHECK(reachable);
}

TEST_CASE("Disconnected fires while the player is still in the roster")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(4, SteamA, "alpha", "");

    int64_t steamId = 0;
    bool stillReachable = false;
    auto sub = players.Disconnected += [&](Player& player) {
        steamId = player.SteamId();
        stillReachable = players.Get(player.Slot()) == &player;
    };

    players.Remove(4);
    CHECK(steamId == SteamA);
    CHECK(stillReachable);
    CHECK(players.Get(4) == nullptr);
}

TEST_CASE("Taking over an occupied slot disconnects the previous occupant first")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(4, SteamA, "alpha", "");

    std::vector<int64_t> events;
    auto left = players.Disconnected += [&](Player& player) { events.push_back(player.SteamId()); };
    auto joined = players.Connected += [&](Player& player) { events.push_back(-player.SteamId()); };

    players.Add(4, SteamB, "bravo", "");
    CHECK(events == std::vector<int64_t>{SteamA, -SteamB});
}

TEST_CASE("Clear disconnects everybody once")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    players.Add(1, SteamA, "alpha", "");
    players.Add(2, SteamB, "bravo", "");

    std::vector<int64_t> gone;
    auto sub = players.Disconnected += [&](Player& player) { gone.push_back(player.SteamId()); };

    players.Clear();
    CHECK(gone == std::vector<int64_t>{SteamA, SteamB});
    CHECK(players.Get(1) == nullptr);
    CHECK(players.BySteamId(SteamB) == nullptr);
}

TEST_CASE("FullyConnected and SettingsChanged only fire for an occupied slot")
{
    SlotEvents slots;
    PlayerManager players = MakeManager(slots);

    int fully = 0;
    int settings = 0;
    auto a = players.FullyConnected += [&](Player&) { ++fully; };
    auto b = players.SettingsChanged += [&](Player&) { ++settings; };

    players.OnClientFullyConnected(2);
    players.OnClientSettingsChanged(2);
    CHECK(fully == 0);
    CHECK(settings == 0);

    players.Add(2, SteamA, "alpha", "");
    players.OnClientFullyConnected(2);
    players.OnClientSettingsChanged(2);
    CHECK(fully == 1);
    CHECK(settings == 1);
}
