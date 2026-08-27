#include "Hooks/AddonRequirements.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using VoltMod::AddonRequirements;
using VoltMod::AddonStep;
using VoltMod::ParseAddonList;

static constexpr int64_t kSteam = 76561198000000001LL;
static constexpr int64_t kOther = 76561198000000002LL;
static constexpr int kAttempts = 3;

TEST_CASE("A required addon is missing until a reconnect credits it")
{
    AddonRequirements requirements;
    requirements.Require(100);

    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100});

    const auto decision = requirements.NextFor(kSteam, 10.0, kAttempts);
    CHECK(decision.Step == AddonStep::Send);
    CHECK(decision.Id == 100);

    // Still missing: sending is not evidence that the client took it.
    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100});

    requirements.CreditReconnect(kSteam, 20.0, 30.0);
    CHECK(requirements.MissingFor(kSteam).empty());
}

TEST_CASE("A reconnect after the timeout does not credit the addon")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.NextFor(kSteam, 10.0, kAttempts);

    requirements.CreditReconnect(kSteam, 100.0, 30.0);
    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100});
}

TEST_CASE("Addons are sent one at a time in the order they were required")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.Require(200);

    CHECK(requirements.NextFor(kSteam, 1.0, kAttempts).Id == 100);
    requirements.CreditReconnect(kSteam, 2.0, 30.0);

    CHECK(requirements.NextFor(kSteam, 3.0, kAttempts).Id == 200);
    requirements.CreditReconnect(kSteam, 4.0, 30.0);

    CHECK(requirements.NextFor(kSteam, 5.0, kAttempts).Step == AddonStep::Nothing);
}

TEST_CASE("Offering the same addon past the attempt cap gives up")
{
    AddonRequirements requirements;
    requirements.Require(100);

    for (int attempt = 1; attempt <= kAttempts; ++attempt)
        CHECK(requirements.NextFor(kSteam, attempt, kAttempts).Step == AddonStep::Send);

    const auto decision = requirements.NextFor(kSteam, 10.0, kAttempts);
    CHECK(decision.Step == AddonStep::GiveUp);
    CHECK(decision.Id == 100);
}

TEST_CASE("A credited reconnect resets the attempt count")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.Require(200);

    requirements.NextFor(kSteam, 1.0, kAttempts);
    requirements.NextFor(kSteam, 2.0, kAttempts);  // second offer of 100
    requirements.CreditReconnect(kSteam, 3.0, 30.0);

    // 200 is a different addon, so it starts its own count and has all its attempts.
    for (int attempt = 1; attempt <= kAttempts; ++attempt)
        CHECK(requirements.NextFor(kSteam, 3.0 + attempt, kAttempts).Step == AddonStep::Send);
}

TEST_CASE("Requirements are reference counted")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.Require(100);

    requirements.Release(100);
    CHECK(requirements.Required() == std::vector<uint64_t>{100});
    CHECK_FALSE(requirements.Empty());

    requirements.Release(100);
    CHECK(requirements.Required().empty());
    CHECK(requirements.Empty());
}

TEST_CASE("Releasing an addon nobody required does nothing")
{
    AddonRequirements requirements;
    requirements.Release(100);
    requirements.Require(100);
    requirements.Release(100);
    requirements.Release(100);

    CHECK(requirements.Empty());
}

TEST_CASE("Addon id zero is refused")
{
    AddonRequirements requirements;
    CHECK_FALSE(requirements.Require(0));
    CHECK_FALSE(requirements.RequireFor(kSteam, 0));
    CHECK(requirements.Empty());
}

TEST_CASE("A per-client requirement reaches only that client")
{
    AddonRequirements requirements;
    requirements.RequireFor(kSteam, 300);

    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{300});
    CHECK(requirements.MissingFor(kOther).empty());
    CHECK(requirements.Required().empty());
    CHECK_FALSE(requirements.Empty());

    requirements.ReleaseFor(kSteam, 300);
    CHECK(requirements.Empty());
}

TEST_CASE("A per-client requirement follows the global list")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.RequireFor(kSteam, 300);

    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100, 300});
}

TEST_CASE("An addon required both globally and per client is listed once")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.RequireFor(kSteam, 100);

    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100});
}

TEST_CASE("An addon the engine already sent is credited rather than resent")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.Require(200);

    // The changelevel message named 100, so it is already on its way.
    requirements.NoteInFlight(kSteam, 100, 5.0);
    requirements.CreditReconnect(kSteam, 6.0, 30.0);

    CHECK(requirements.NextFor(kSteam, 7.0, kAttempts).Id == 200);
}

TEST_CASE("An in-flight addon from the engine costs no attempt")
{
    AddonRequirements requirements;
    requirements.Require(100);

    for (int attempt = 0; attempt < 10; ++attempt)
        requirements.NoteInFlight(kSteam, 100, attempt);

    CHECK(requirements.NextFor(kSteam, 20.0, kAttempts).Step == AddonStep::Send);
}

TEST_CASE("Forgetting clients keeps their requirements but drops their progress")
{
    AddonRequirements requirements;
    requirements.Require(100);
    requirements.RequireFor(kOther, 300);
    requirements.NextFor(kSteam, 1.0, kAttempts);
    requirements.CreditReconnect(kSteam, 2.0, 30.0);
    CHECK(requirements.MissingFor(kSteam).empty());

    requirements.ForgetClients();

    CHECK(requirements.MissingFor(kSteam) == std::vector<uint64_t>{100});
    CHECK(requirements.MissingFor(kOther) == std::vector<uint64_t>{100, 300});
}

TEST_CASE("An addons field parses as a comma separated list")
{
    CHECK(ParseAddonList("") == std::vector<uint64_t>{});
    CHECK(ParseAddonList("100") == std::vector<uint64_t>{100});
    CHECK(ParseAddonList("100,200,300") == std::vector<uint64_t>{100, 200, 300});
}

TEST_CASE("A malformed addons entry is skipped rather than misread")
{
    CHECK(ParseAddonList("100,,200") == std::vector<uint64_t>{100, 200});
    CHECK(ParseAddonList("100,abc,200") == std::vector<uint64_t>{100, 200});
    CHECK(ParseAddonList("100x") == std::vector<uint64_t>{});
    CHECK(ParseAddonList("0") == std::vector<uint64_t>{});
}
