#include <VoltMod/Players/Targeting.hpp>
#include <doctest/doctest.h>

using namespace VoltMod::Players;
using Kind = TargetKind;

namespace
{

// Slot layout: 0 Alice (T, alive), 1 Bob (CT, alive), 2 Bobby (CT, dead), 3 bot "Chick" (T, alive),
// 4 Spec (spectator). Caller is Alice (slot 0).
std::vector<PlayerView> Roster()
{
    return {
        {.Slot = 0, .SteamId = 76561197960287930, .Name = "Alice", .Team = 2, .Alive = true, .Bot = false},
        {.Slot = 1, .SteamId = 76561197960287931, .Name = "Bob", .Team = 3, .Alive = true, .Bot = false},
        {.Slot = 2, .SteamId = 76561197960287932, .Name = "Bobby", .Team = 3, .Alive = false, .Bot = false},
        {.Slot = 3, .SteamId = 0, .Name = "Chick", .Team = 2, .Alive = true, .Bot = true},
        {.Slot = 4, .SteamId = 76561197960287934, .Name = "Spec", .Team = 1, .Alive = false, .Bot = false},
    };
}

constexpr int Caller = 0;

std::size_t FirstIndex(std::size_t)
{
    return 0;
}

int FrontSlot(const std::expected<std::vector<int>, TargetFailure>& r)
{
    return (r && !r->empty()) ? r->front() : -99;
}

std::size_t Size(const std::expected<std::vector<int>, TargetFailure>& r)
{
    return r ? r->size() : std::size_t{0};
}

bool FailedWith(const std::expected<std::vector<int>, TargetFailure>& r, TargetError error)
{
    return !r && r.error().Error == error;
}

}  // namespace

TEST_CASE("ParseTargetToken: selectors")
{
    CHECK(ParseTargetToken("@all").Kind == Kind::All);
    CHECK(ParseTargetToken("@*").Kind == Kind::All);
    CHECK(ParseTargetToken("@ME").Kind == Kind::Me);
    CHECK(ParseTargetToken("@!me").Kind == Kind::NotMe);
    CHECK(ParseTargetToken("@t").Kind == Kind::Team);
    CHECK_EQ(ParseTargetToken("@t").Team, 2);
    CHECK_EQ(ParseTargetToken("@CT").Team, 3);
    CHECK(ParseTargetToken("@spec").Kind == Kind::Team);
    CHECK_EQ(ParseTargetToken("@spec").Team, 1);
    CHECK(ParseTargetToken("@dead").Kind == Kind::Dead);
    CHECK(ParseTargetToken("@alive").Kind == Kind::Alive);
    CHECK(ParseTargetToken("@bot").Kind == Kind::Bots);
    CHECK(ParseTargetToken("@human").Kind == Kind::Humans);
    CHECK(ParseTargetToken("@random").Kind == Kind::Random);
    CHECK_EQ(ParseTargetToken("@randomct").Team, 3);
}

TEST_CASE("ParseTargetToken: slot, steamid, name fallback")
{
    auto slot = ParseTargetToken("#3");
    CHECK(slot.Kind == Kind::Slot);
    CHECK_EQ(slot.Slot, 3);

    auto id = ParseTargetToken("76561197960287930");
    CHECK(id.Kind == Kind::SteamId);
    CHECK_EQ(id.SteamId, 76561197960287930);

    CHECK(ParseTargetToken("[U:1:22202]").Kind == Kind::SteamId);
    CHECK(ParseTargetToken("STEAM_0:0:11101").Kind == Kind::SteamId);

    CHECK(ParseTargetToken("#abc").Kind == Kind::Name);
    auto bob = ParseTargetToken("BOB");
    CHECK(bob.Kind == Kind::Name);
    CHECK_EQ(bob.Needle, std::string("bob"));
}

TEST_CASE("FilterRoster: @all and multi rules")
{
    auto roster = Roster();
    auto all = FilterRoster(roster, ParseTargetToken("@all"), {.AllowMultiple = true}, Caller);
    CHECK_EQ(Size(all), std::size_t{5});

    auto single = FilterRoster(roster, ParseTargetToken("@all"), {}, Caller);
    CHECK(FailedWith(single, TargetError::MultiNotAllowed));
}

TEST_CASE("FilterRoster: name tiers prefer exact over prefix over substring")
{
    auto roster = Roster();
    // "bob" matches Bob exactly even though Bobby also prefixes.
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("bob"), {}, Caller)), 1);

    // "bobb" prefixes only Bobby.
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("bobb"), {}, Caller)), 2);

    // "ob" substring-matches Bob and Bobby -> ambiguous with a count.
    auto ambiguous = FilterRoster(roster, ParseTargetToken("ob"), {}, Caller);
    CHECK(FailedWith(ambiguous, TargetError::Ambiguous));
    CHECK_EQ(ambiguous ? 0 : ambiguous.error().Count, 2);
}

TEST_CASE("FilterRoster: me, not-me, team, spectators")
{
    auto roster = Roster();
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("@me"), {}, Caller)), 0);
    CHECK_EQ(Size(FilterRoster(roster, ParseTargetToken("@!me"), {.AllowMultiple = true}, Caller)), std::size_t{4});
    CHECK_EQ(Size(FilterRoster(roster, ParseTargetToken("@ct"), {.AllowMultiple = true}, Caller)), std::size_t{2});
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("@spec"), {.AllowMultiple = true}, Caller)), 4);
}

TEST_CASE("FilterRoster: dead/bot rules produce typed errors")
{
    auto roster = Roster();
    // Bobby is dead; a command that disallows dead targets says so.
    CHECK(FailedWith(FilterRoster(roster, ParseTargetToken("bobby"), {.AllowDead = false}, Caller),
                     TargetError::DeadNotAllowed));

    CHECK(FailedWith(FilterRoster(roster, ParseTargetToken("chick"), {.AllowBots = false}, Caller),
                     TargetError::BotNotAllowed));
}

TEST_CASE("FilterRoster: immunity blocks with Immune only when nobody remains")
{
    auto roster = Roster();
    roster[1].Targetable = false;  // Bob immune

    CHECK(FailedWith(FilterRoster(roster, ParseTargetToken("bob"), {}, Caller), TargetError::Immune));

    // @ct: Bob immune but Bobby remains -> silently narrowed to Bobby.
    auto narrowed = FilterRoster(roster, ParseTargetToken("@ct"), {.AllowMultiple = true}, Caller);
    CHECK_EQ(Size(narrowed), std::size_t{1});
    CHECK_EQ(FrontSlot(narrowed), 2);
}

TEST_CASE("FilterRoster: random kinds resolve to one entry")
{
    auto roster = Roster();
    auto random = FilterRoster(roster, ParseTargetToken("@random"), {}, Caller, FirstIndex);
    CHECK_EQ(Size(random), std::size_t{1});
    CHECK_EQ(FrontSlot(random), 0);

    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("@randomct"), {}, Caller, FirstIndex)), 1);
}

TEST_CASE("FilterRoster: slot and steamid forms")
{
    auto roster = Roster();
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("#1"), {}, Caller)), 1);
    CHECK_EQ(FrontSlot(FilterRoster(roster, ParseTargetToken("76561197960287932"), {}, Caller)), 2);
    CHECK(FailedWith(FilterRoster(roster, ParseTargetToken("#9"), {}, Caller), TargetError::NoMatch));
}
