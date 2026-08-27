#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::ArgKind;
using VoltMod::BoundArg;
using VoltMod::Caller;
using VoltMod::CommandBuilder;
using VoltMod::CommandDefinition;
using VoltMod::CommandSignature;
using VoltMod::Reply;
using VoltMod::Result;
using VoltMod::SlotEvents;
using VoltMod::Translations;

namespace Args = VoltMod::Args;

// The signature rules are compile-time, so they are asserted as a concept rather than by trying
// to compile the calls they reject - which would fail the build instead of failing a test.
static_assert(CommandSignature<>);
static_assert(CommandSignature<Args::Target, Args::Duration, Args::Opt<Args::Rest>>);
static_assert(CommandSignature<Args::Int, Args::Opt<Args::Int>>);
static_assert(CommandSignature<Args::Rest>);
static_assert(!CommandSignature<Args::Opt<Args::Int>, Args::Int>, "an Opt may only trail");
static_assert(!CommandSignature<Args::Rest, Args::Int>, "a Rest may only be last");
static_assert(!CommandSignature<Args::Opt<Args::Rest>, Args::Word>, "an Opt Rest is still last");
static_assert(!CommandSignature<int>, "a plain type is not a command argument");
static_assert(!CommandSignature<std::string>, "nor is a string");

/** Captures what a builder produced, standing in for the manager that would install it. */
struct Installed
{
    CommandDefinition Def;
    int Installs = 0;

    CommandBuilder Builder(std::string_view name)
    {
        return CommandBuilder(
            [this](CommandDefinition def) {
                Def = std::move(def);
                ++Installs;
            },
            name);
    }
};

TEST_CASE("The metadata methods fill the definition and each returns the builder")
{
    Installed installed;
    installed.Builder("voice_mute")
        .Describe("Voice-mute a player.")
        .Alias("vmute")
        .Alias("mute")
        .Permission("m")
        .UsageKey("cmd.muteUsage")
        .Run([](Caller c) -> Result<Reply> { return c.Ok("cmd.ok"); });

    CHECK(installed.Def.Name == "voice_mute");
    CHECK(installed.Def.Description == "Voice-mute a player.");
    CHECK(installed.Def.PermissionName == "m");
    CHECK(installed.Def.UsageKey == "cmd.muteUsage");
    REQUIRE(installed.Def.Aliases.size() == 2);
    CHECK(installed.Def.Aliases[0] == "vmute");
    CHECK(installed.Def.Chat);
    CHECK_FALSE(installed.Def.Console);
    CHECK(installed.Def.Args.empty());
}

TEST_CASE("Console adds the console surface and ConsoleOnly takes chat away")
{
    Installed installed;
    installed.Builder("bhop_reload").Console().Run([](Caller) -> Result<Reply> { return Reply::Silent(); });
    CHECK(installed.Def.Chat);
    CHECK(installed.Def.Console);

    installed.Builder("bhop_player").ConsoleOnly().Run([](Caller) -> Result<Reply> { return Reply::Silent(); });
    CHECK_FALSE(installed.Def.Chat);
    CHECK(installed.Def.Console);
}

TEST_CASE("The handler's parameter list is the argument descriptor")
{
    Installed installed;
    installed.Builder("ban").Run(
        [](Caller c, Args::Target, Args::Duration, Args::Opt<Args::Rest>) -> Result<Reply> { return c.Ok("ok"); });

    REQUIRE(installed.Def.Args.size() == 3);
    CHECK(installed.Def.Args[0].Kind == ArgKind::Target);
    CHECK_FALSE(installed.Def.Args[0].Optional);
    CHECK(installed.Def.Args[1].Kind == ArgKind::Duration);
    CHECK_FALSE(installed.Def.Args[1].Optional);
    CHECK(installed.Def.Args[2].Kind == ArgKind::Rest);
    CHECK(installed.Def.Args[2].Optional);
}

TEST_CASE("Every argument type maps to its own kind")
{
    Installed installed;
    installed.Builder("everything")
        .Run([](Caller c, Args::Target, Args::SteamId, Args::PlayerOrSteamId, Args::Int, Args::Word,
                Args::Rest) -> Result<Reply> { return c.Ok("ok"); });

    REQUIRE(installed.Def.Args.size() == 6);
    CHECK(installed.Def.Args[0].Kind == ArgKind::Target);
    CHECK(installed.Def.Args[1].Kind == ArgKind::SteamId);
    CHECK(installed.Def.Args[2].Kind == ArgKind::PlayerOrSteamId);
    CHECK(installed.Def.Args[3].Kind == ArgKind::Int);
    CHECK(installed.Def.Args[4].Kind == ArgKind::Word);
    CHECK(installed.Def.Args[5].Kind == ArgKind::Rest);
}

TEST_CASE("The trampoline unpacks bound arguments back into the parameter list")
{
    Installed installed;
    installed.Builder("dumpcmd").Run(
        [](Caller c, Args::Int slot, Args::Opt<Args::Int> ticks, Args::Opt<Args::Rest> note) -> Result<Reply> {
            return c.Ok(std::to_string(slot.Value) + "/" + std::to_string(ticks.Value ? ticks.Value->Value : -1) + "/" +
                        (note.Value ? note.Value->Value : std::string("-")));
        });

    SlotEvents slots;
    Translations texts{slots};
    const Caller caller{.Player = nullptr, .Slot = -1, .Tr = texts, .Send = {}};

    const std::vector<BoundArg> full{Args::Int{7}, Args::Int{128}, Args::Rest{"why not"}};
    auto answered = installed.Def.Invoke(caller, full);
    REQUIRE(answered.has_value());
    CHECK(answered->Text == "7/128/why not");

    const std::vector<BoundArg> partial{Args::Int{2}, std::monostate{}, std::monostate{}};
    auto omitted = installed.Def.Invoke(caller, partial);
    REQUIRE(omitted.has_value());
    CHECK(omitted->Text == "2/-1/-");
}

TEST_CASE("Run installs the command and hands nothing back")
{
    Installed installed;
    installed.Builder("ping").Run([](Caller c) -> Result<Reply> { return c.Ok("cmd.pong"); });
    CHECK(installed.Installs == 1);
    CHECK(installed.Def.Name == "ping");
}

TEST_CASE("Caller Fail is a failure carrying both the key and the localized line")
{
    SlotEvents slots;
    Translations texts{slots};
    std::vector<std::string> lines;
    const Caller caller{
        .Player = nullptr, .Slot = -1, .Tr = texts, .Send = [&lines](const std::string& l) { lines.push_back(l); }};

    Result<Reply> failed = caller.Fail("target.immune", {{"token", "Bob"}});
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().Key == "target.immune");
    CHECK(failed.error().Detail == "'Bob' is immune to that.");

    Result<Reply> ok = caller.Ok("cmd.badNumber", {{"token", "x"}});
    REQUIRE(ok.has_value());
    CHECK(ok->Text == "'x' is not a valid number.");

    caller.Say("cmd.noPermission");
    caller.SayRaw("  a row");
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "You do not have permission to use this command.");
    CHECK(lines[1] == "  a row");
}
