#include "Commands/CommandRouter.hpp"
#include "Commands/CommandSyntax.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::ArgBinder;
using VoltMod::ArgDesc;
using VoltMod::ArgKind;
using VoltMod::BindArgs;
using VoltMod::BoundArg;
using VoltMod::Caller;
using VoltMod::CommandDefinition;
using VoltMod::CommandRouter;
using VoltMod::Origin;
using VoltMod::Player;
using VoltMod::PlayerManager;
using VoltMod::Policy;
using VoltMod::Reply;
using VoltMod::Result;
using VoltMod::SlotEvents;
using VoltMod::TargetError;
using VoltMod::TargetFailure;
using VoltMod::TargetRules;
using VoltMod::Translations;

namespace CommandSyntax = VoltMod::CommandSyntax;

namespace Args = VoltMod::Args;

/** Engine-free binder backed by a fixed roster or failure. */
struct StubBinder final : ArgBinder
{
    std::vector<Player*> Roster;
    TargetFailure Failure{TargetError::NoMatch};
    bool Succeed = true;
    /** Last Resolve input, used to check the router's rules. */
    std::string LastToken;
    bool LastAllowedMultiple = false;

    std::expected<std::vector<Player*>, TargetFailure> Resolve(std::string_view token, Player* /*caller*/,
                                                               const TargetRules& rules) override
    {
        LastToken = std::string(token);
        LastAllowedMultiple = rules.AllowMultiple;
        if (!Succeed)
            return std::unexpected(Failure);
        return Roster;
    }
};

struct Fixture
{
    SlotEvents Slots;
    PlayerManager Players{Slots, nullptr};
    Policy Rules{Players};
    Translations Texts{Slots};
    CommandRouter Router{Rules, Texts};
    StubBinder Binder;
    std::vector<std::string> Lines;

    Fixture()
    {
        Rules.HasPermission = [](int64_t, std::string_view) { return true; };
    }

    Player& AddPlayer(int slot, int64_t steamId) { return *Players.Add(slot, steamId, "player", ""); }

    void Say(const std::string& line) { Lines.push_back(line); }

    /** Run @p name with @p tokens as if it had been typed by @p caller. */
    void Run(const std::string& name, std::vector<std::string> tokens, Player* caller, Origin origin = Origin::Chat)
    {
        const CommandDefinition* def = Router.Find(name);
        REQUIRE(def != nullptr);
        Router.Dispatch(*def, caller, tokens, origin, Binder, [this](const std::string& l) { Say(l); });
    }
};

static CommandDefinition Echo(std::string name, std::vector<ArgDesc> args, std::vector<BoundArg>* seen)
{
    CommandDefinition def;
    def.Name = std::move(name);
    def.Args = std::move(args);
    def.Invoke = [seen](const Caller& c, std::span<const BoundArg> bound) -> Result<Reply> {
        if (seen)
            seen->assign(bound.begin(), bound.end());
        return c.Ok("ok");
    };
    return def;
}

TEST_CASE("Tokenize splits on spaces and drops the blanks repeated spaces make")
{
    const auto tokens = CommandSyntax::Tokenize("  ban   Bob    30  ");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[0] == "ban");
    CHECK(tokens[1] == "Bob");
    CHECK(tokens[2] == "30");
}

TEST_CASE("Tokenize keeps a quoted run as one token")
{
    const auto tokens = CommandSyntax::Tokenize("ban Bob 30 \"bad aim\"");
    REQUIRE(tokens.size() == 4);
    CHECK(tokens[3] == "bad aim");
}

TEST_CASE("Tokenize treats a backslash-escaped quote as a literal quote")
{
    const auto tokens = CommandSyntax::Tokenize("say \"he said \\\"hi\\\" twice\"");
    REQUIRE(tokens.size() == 2);
    CHECK(tokens[1] == "he said \"hi\" twice");
}

TEST_CASE("Tokenize keeps an explicit empty token but not an implicit one")
{
    const auto tokens = CommandSyntax::Tokenize("set \"\"  x");
    REQUIRE(tokens.size() == 3);
    CHECK(tokens[1].empty());
    CHECK(tokens[2] == "x");
}

TEST_CASE("StripPrefix accepts both chat prefixes and nothing else")
{
    CHECK(CommandSyntax::StripPrefix("!ban Bob").value() == "ban Bob");
    CHECK(CommandSyntax::StripPrefix(".ban Bob").value() == "ban Bob");
    CHECK_FALSE(CommandSyntax::StripPrefix("ban Bob").has_value());
    CHECK_FALSE(CommandSyntax::StripPrefix("hello").has_value());
    // A bare prefix names no command, so it is not one.
    CHECK_FALSE(CommandSyntax::StripPrefix("!").has_value());
}

TEST_CASE("A command is found by name and by alias, case-insensitively")
{
    Fixture f;
    CommandDefinition def = Echo("voice_mute", {}, nullptr);
    def.Aliases = {"vmute", "mute"};
    REQUIRE(f.Router.Add(std::move(def)));

    CHECK(f.Router.Find("voice_mute") != nullptr);
    CHECK(f.Router.Find("VOICE_MUTE") != nullptr);
    CHECK(f.Router.Find("vmute") != nullptr);
    CHECK(f.Router.Find("MUTE") != nullptr);
    CHECK(f.Router.Find("unmute") == nullptr);
}

TEST_CASE("A second registration of the same name is refused")
{
    Fixture f;
    CHECK(f.Router.Add(Echo("ban", {}, nullptr)));
    CHECK_FALSE(f.Router.Add(Echo("ban", {}, nullptr)));
    CHECK(f.Router.Count() == 1);
}

TEST_CASE("Clear drops every command and its aliases")
{
    Fixture f;
    CommandDefinition def = Echo("ban", {}, nullptr);
    def.Aliases = {"b"};
    f.Router.Add(std::move(def));
    f.Router.Add(Echo("kick", {}, nullptr));

    f.Router.Clear();
    CHECK(f.Router.Find("ban") == nullptr);
    CHECK(f.Router.Find("b") == nullptr);
    CHECK(f.Router.Find("kick") == nullptr);
    CHECK(f.Router.Count() == 0);
}

TEST_CASE("Too few arguments reply with the usage line and never reach the handler")
{
    Fixture f;
    std::vector<BoundArg> seen{BoundArg{}};
    f.Router.Add(Echo("ban", {{ArgKind::Target}, {ArgKind::Duration}}, &seen));

    f.Run("ban", {"Bob"}, nullptr);

    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "Usage: !ban <target> <duration>");
    CHECK(seen.size() == 1);  // untouched: the handler did not run
}

TEST_CASE("The usage line carries the chat prefix only on the chat surface")
{
    Fixture f;
    f.Router.Add(Echo("ban", {{ArgKind::Target}, {.Kind = ArgKind::Rest, .Optional = true}}, nullptr));
    const CommandDefinition* def = f.Router.Find("ban");

    CHECK(f.Router.Usage(*def, -1, Origin::Chat) == "Usage: !ban <target> [reason]");
    CHECK(f.Router.Usage(*def, -1, Origin::Console) == "Usage: ban <target> [reason]");
}

TEST_CASE("A UsageKey replaces the derived usage line")
{
    Fixture f;
    CommandDefinition def = Echo("unban", {{ArgKind::SteamId}}, nullptr);
    def.UsageKey = "cmd.unbanUsage";
    f.Router.Add(std::move(def));

    f.Run("unban", {}, nullptr);

    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "cmd.unbanUsage");  // no table loaded, so the key comes back verbatim
}

TEST_CASE("Too many arguments are refused rather than dropped")
{
    Fixture f;
    f.Binder.Roster = {&f.AddPlayer(1, 76561198000000001LL)};
    f.Router.Add(Echo("slap", {{ArgKind::Target}, {.Kind = ArgKind::Int, .Optional = true}}, nullptr));

    f.Run("slap", {"Bob", "5", "extra"}, nullptr);

    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "Too many arguments. Usage: !slap <target> [number]");
}

TEST_CASE("A Rest argument swallows the remainder, so nothing is ever extra")
{
    Fixture f;
    std::vector<BoundArg> seen;
    f.Router.Add(Echo("say", {{ArgKind::Rest}}, &seen));

    f.Run("say", {"one", "two", "three", "four"}, nullptr);

    REQUIRE(seen.size() == 1);
    CHECK(std::get<Args::Rest>(seen[0]).Value == "one two three four");
    CHECK(f.Lines.size() == 1);
}

TEST_CASE("An omitted trailing Opt binds as nothing and the handler still runs")
{
    Fixture f;
    std::vector<BoundArg> seen;
    f.Router.Add(Echo("warn", {{ArgKind::Int}, {.Kind = ArgKind::Rest, .Optional = true}}, &seen));

    f.Run("warn", {"3"}, nullptr);

    REQUIRE(seen.size() == 2);
    CHECK(std::get<Args::Int>(seen[0]).Value == 3);
    CHECK(std::holds_alternative<std::monostate>(seen[1]));
}

TEST_CASE("A present Opt binds like the type it wraps")
{
    Fixture f;
    std::vector<BoundArg> seen;
    f.Router.Add(Echo("dumpcmd", {{ArgKind::Int}, {.Kind = ArgKind::Int, .Optional = true}}, &seen));

    f.Run("dumpcmd", {"2", "128"}, nullptr);

    REQUIRE(seen.size() == 2);
    CHECK(std::get<Args::Int>(seen[0]).Value == 2);
    CHECK(std::get<Args::Int>(seen[1]).Value == 128);
}

TEST_CASE("Durations read a bare number as minutes and a suffix as itself")
{
    Fixture f;
    std::vector<BoundArg> seen;
    f.Router.Add(Echo("ban", {{ArgKind::Duration}}, &seen));

    f.Run("ban", {"30"}, nullptr);
    CHECK(std::get<Args::Duration>(seen[0]).Value.count() == 30 * 60);

    f.Run("ban", {"30s"}, nullptr);
    CHECK(std::get<Args::Duration>(seen[0]).Value.count() == 30);

    f.Run("ban", {"perm"}, nullptr);
    CHECK(std::get<Args::Duration>(seen[0]).Value.count() == 0);
}

TEST_CASE("Argument parse failures map to their own keys")
{
    Fixture f;
    f.Router.Add(Echo("ban", {{ArgKind::Duration}}, nullptr));
    f.Router.Add(Echo("unban", {{ArgKind::SteamId}}, nullptr));
    f.Router.Add(Echo("dumpcmd", {{ArgKind::Int}}, nullptr));
    const CommandDefinition* ban = f.Router.Find("ban");
    const CommandDefinition* unban = f.Router.Find("unban");
    const CommandDefinition* dump = f.Router.Find("dumpcmd");

    const std::vector<std::string> bad{"nonsense"};
    CHECK(BindArgs(*ban, bad, nullptr, f.Binder).error().Key == "cmd.badDuration");
    CHECK(BindArgs(*unban, bad, nullptr, f.Binder).error().Key == "cmd.badSteamId");
    CHECK(BindArgs(*dump, bad, nullptr, f.Binder).error().Key == "cmd.badNumber");
}

TEST_CASE("Target failures map to the key that explains them")
{
    Fixture f;
    f.Router.Add(Echo("kick", {{ArgKind::Target}}, nullptr));
    const CommandDefinition* kick = f.Router.Find("kick");
    const std::vector<std::string> token{"Bob"};
    f.Binder.Succeed = false;

    f.Binder.Failure = {TargetError::NoMatch};
    CHECK(BindArgs(*kick, token, nullptr, f.Binder).error().Key == "target.noMatch");

    f.Binder.Failure = {TargetError::Immune};
    CHECK(BindArgs(*kick, token, nullptr, f.Binder).error().Key == "target.immune");

    f.Binder.Failure = {TargetError::Ambiguous, 3};
    auto ambiguous = BindArgs(*kick, token, nullptr, f.Binder);
    CHECK(ambiguous.error().Key == "target.ambiguous");
    CHECK(ambiguous.error().Vars.at("count") == "3");

    f.Binder.Failure = {TargetError::DeadNotAllowed};
    CHECK(BindArgs(*kick, token, nullptr, f.Binder).error().Key == "target.dead");

    f.Binder.Failure = {TargetError::BotNotAllowed};
    CHECK(BindArgs(*kick, token, nullptr, f.Binder).error().Key == "target.bot");
}

TEST_CASE("PlayerOrSteamId prefers the online player and falls back to a bare id")
{
    Fixture f;
    Player& online = f.AddPlayer(1, 76561198000000001LL);
    f.Binder.Roster = {&online};

    std::vector<BoundArg> seen;
    f.Router.Add(Echo("freeze_admin", {{ArgKind::PlayerOrSteamId}}, &seen));

    f.Run("freeze_admin", {"Bob"}, nullptr);
    CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).Online == &online);
    CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).SteamId == 76561198000000001LL);

    // Nobody online answers to it, but a bare SteamID64 still names an offline admin.
    f.Binder.Succeed = false;
    f.Run("freeze_admin", {"76561198000000009"}, nullptr);
    CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).Online == nullptr);
    CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).SteamId == 76561198000000009LL);

    // A name that matches nobody is still a target error, not an offline id.
    f.Lines.clear();
    f.Run("freeze_admin", {"Nobody"}, nullptr);
    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "No player matches 'Nobody'.");
}

// Only NoMatch may fall back to a bare SteamID; immunity and other failures must remain errors.
TEST_CASE("PlayerOrSteamId does not turn a refused target into an offline one")
{
    Fixture f;
    f.Binder.Succeed = false;

    std::vector<BoundArg> seen;
    f.Router.Add(Echo("ban", {{ArgKind::PlayerOrSteamId}}, &seen));

    SUBCASE("an immune numeric target is refused, not bound offline")
    {
        f.Binder.Failure = {TargetError::Immune};
        f.Run("ban", {"76561198000000009"}, nullptr);

        CHECK(seen.empty());
        REQUIRE(f.Lines.size() == 1);
        CHECK(f.Lines[0].find("immune") != std::string::npos);
    }

    SUBCASE("an ambiguous numeric token is refused, not bound offline")
    {
        f.Binder.Failure = {TargetError::Ambiguous, 2};
        f.Run("ban", {"76561198000000009"}, nullptr);

        CHECK(seen.empty());
        CHECK(f.Lines.size() == 1);
    }

    SUBCASE("a dead-target refusal on a numeric token is still a refusal")
    {
        f.Binder.Failure = {TargetError::DeadNotAllowed};
        f.Run("ban", {"76561198000000009"}, nullptr);

        CHECK(seen.empty());
        CHECK(f.Lines.size() == 1);
    }

    SUBCASE("no match still binds the bare id, which is the point of the type")
    {
        f.Binder.Failure = {TargetError::NoMatch};
        f.Run("ban", {"76561198000000009"}, nullptr);

        REQUIRE(seen.size() == 1);
        CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).Online == nullptr);
        CHECK(std::get<Args::PlayerOrSteamId>(seen[0]).SteamId == 76561198000000009LL);
    }
}

TEST_CASE("A permission is checked for a player and skipped for the console")
{
    Fixture f;
    Player& player = f.AddPlayer(1, 76561198000000001LL);
    CommandDefinition def = Echo("ban", {}, nullptr);
    def.PermissionName = "b";
    f.Router.Add(std::move(def));

    f.Rules.HasPermission = [](int64_t, std::string_view) { return false; };
    f.Run("ban", {}, &player);
    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "You do not have permission to use this command.");

    // The console is the server itself: no SteamID to check, nothing above it to deny it.
    f.Lines.clear();
    f.Run("ban", {}, nullptr, Origin::Console);
    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "ok");
}

TEST_CASE("Commands declaring a permission are named for the load report")
{
    Fixture f;
    CommandDefinition gated = Echo("ban", {}, nullptr);
    gated.PermissionName = "b";
    f.Router.Add(std::move(gated));
    f.Router.Add(Echo("report", {}, nullptr));

    const auto named = f.Router.NamesWithPermission();
    REQUIRE(named.size() == 1);
    CHECK(named[0] == "ban");
}

TEST_CASE("A handler failure replies with its own localized line and a success with its reply")
{
    Fixture f;
    CommandDefinition def;
    def.Name = "check";
    def.Invoke = [](const Caller& c, std::span<const BoundArg>) -> Result<Reply> {
        return c.Fail("cheatCheck.noActiveCheck");
    };
    f.Router.Add(std::move(def));

    f.Run("check", {}, nullptr);
    REQUIRE(f.Lines.size() == 1);
    CHECK(f.Lines[0] == "cheatCheck.noActiveCheck");
}

TEST_CASE("A silent reply sends nothing")
{
    Fixture f;
    CommandDefinition def;
    def.Name = "admin";
    def.Invoke = [](const Caller&, std::span<const BoundArg>) -> Result<Reply> { return Reply::Silent(); };
    f.Router.Add(std::move(def));

    f.Run("admin", {}, nullptr);
    CHECK(f.Lines.empty());
}

TEST_CASE("Say lines precede the final reply")
{
    Fixture f;
    CommandDefinition def;
    def.Name = "who";
    def.Invoke = [](const Caller& c, std::span<const BoundArg>) -> Result<Reply> {
        c.SayRaw("  #1 Bob");
        c.SayRaw("  #2 Alice");
        return c.Ok("cmd.done");
    };
    f.Router.Add(std::move(def));

    f.Run("who", {}, nullptr);
    REQUIRE(f.Lines.size() == 3);
    CHECK(f.Lines[0] == "  #1 Bob");
    CHECK(f.Lines[2] == "cmd.done");
}

TEST_CASE("An Int argument accepts the whole int range and rejects what is outside it")
{
    Fixture f;
    std::vector<BoundArg> seen;
    f.Router.Add(Echo("depth", {{ArgKind::Int}}, &seen));

    f.Run("depth", {"2147483647"}, nullptr);
    REQUIRE(seen.size() == 1);
    CHECK(std::get<Args::Int>(seen[0]).Value == 2147483647);

    seen.clear();
    f.Run("depth", {"-2147483648"}, nullptr);
    REQUIRE(seen.size() == 1);
    CHECK(std::get<Args::Int>(seen[0]).Value == -2147483648);

    for (std::string_view token : {"2147483648", "-2147483649", "9223372036854775807"})
    {
        seen.clear();
        f.Lines.clear();
        f.Run("depth", {std::string(token)}, nullptr);

        CHECK(seen.empty());
        CHECK(f.Lines.size() == 1);
    }
}
