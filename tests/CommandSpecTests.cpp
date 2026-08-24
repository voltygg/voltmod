#include <CS2Kit/Commands/CommandSpec.hpp>
#include <doctest/doctest.h>

using namespace CS2Kit::Commands;

TEST_CASE("A spec is reachable only from the surfaces it declares")
{
    CommandSpec chatOnly{.Name = "ban"};  // Surface::Chat is the default
    CHECK(ReachableFrom(chatOnly, Surface::Chat));
    CHECK_FALSE(ReachableFrom(chatOnly, Surface::Console));

    CommandSpec both{.Name = "kick", .Surfaces = Surface::Chat | Surface::Console};
    CHECK(ReachableFrom(both, Surface::Chat));
    CHECK(ReachableFrom(both, Surface::Console));
}

TEST_CASE("A console-only command is not reachable from chat")
{
    // The regression: the Chat bit was never read, so an operator command registered as
    // console-only - and therefore carrying no Permission - answered any player who typed it.
    CommandSpec consoleOnly{.Name = "bhop_player", .Surfaces = Surface::Console};

    CHECK_FALSE(ReachableFrom(consoleOnly, Surface::Chat));
    CHECK(ReachableFrom(consoleOnly, Surface::Console));
}

TEST_CASE("Extra arguments are refused against the spec's arity")
{
    CommandSpec spec{.Name = "slap", .Args = {Target(), Int()}};

    CHECK_FALSE(TooManyArguments(spec, 0));
    CHECK_FALSE(TooManyArguments(spec, 2));
    CHECK(TooManyArguments(spec, 3));
}

TEST_CASE("A ReasonTail swallows the remainder, so nothing is ever extra")
{
    CommandSpec spec{.Name = "ban", .Args = {Target(), Duration(), ReasonTail()}};

    CHECK_FALSE(TooManyArguments(spec, 3));
    CHECK_FALSE(TooManyArguments(spec, 12));
}

TEST_CASE("Derived usage takes its prefix from the calling surface")
{
    CommandSpec spec{.Name = "ban", .Args = {Target(), Duration(), ReasonTail()}};

    CHECK(DeriveUsage(spec, "!") == "!ban <target> <duration> [reason]");
    CHECK(DeriveUsage(spec, ".") == ".ban <target> <duration> [reason]");
    // The console types no prefix; claiming one sent operators to a form that does not work.
    CHECK(DeriveUsage(spec, "") == "ban <target> <duration> [reason]");
}

TEST_CASE("Derived usage names each argument kind and marks the optional ones")
{
    CommandSpec spec{.Name = "x", .Args = {TargetOrSteamId(), SteamId64(), Int(), Word(), Word(false)}};

    CHECK(DeriveUsage(spec, "!") == "!x <target|steamId> <steamId> <number> <value> [value]");
}

TEST_CASE("A spec with no arguments derives just its name")
{
    CHECK(DeriveUsage(CommandSpec{.Name = "admin_reload"}, "!") == "!admin_reload");
}
