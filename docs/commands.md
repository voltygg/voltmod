# Commands {#commands_guide}

[TOC]

```cpp
#include <VoltMod/Api.hpp>

using VoltMod::Caller;
namespace Args = VoltMod::Args;

runtime.Commands.Add("ban")
    .Describe("Ban a player.")
    .Alias("b")
    .Permission("b")
    .Run([&app](Caller c, Args::Target t, Args::Duration d, Args::Opt<Args::Rest> why) {
        std::string name = t->Name();                // capture first: a ban drops the target
        std::string reason = why.ValueOr(c.Translations.Get("reason.bannedByAdmin"));
        if (!app.Ban(*c.Player, *t.Value, reason, d.Value))
            return c.Fail("cmd.banFailed");
        return c.Ok("cmd.banSuccess", {{"name", name}});
    });
```

`Run` installs the command and hands nothing back. A command lives as long as the
@ref VoltMod::CommandManager that owns it, and VoltMod drops every one before destroying the
plugin, so a handler cannot outlive the state it captured. There is no way to unregister
one command. The builder is single use: `Add` starts a new one.

Per invocation the manager resolves the name or alias, authorizes the caller, checks arity,
binds every argument, runs the handler, and routes the reply through `Policy::Reply` (falling
back to `Messages::Send`). Any failure stops before the handler and replies with a localized
error.

## Argument types

Each handler parameter after the leading @ref VoltMod::Caller is one argument.

| Parameter | Consumes | Read with |
| --- | --- | --- |
| `Args::Target` | one token via the selector grammar, one online player | `t.Value`, never null; `t->Name()` reads through it |
| `Args::Targets` | one token naming several players (`@all`, `@t`, ...) | `t.Value`, a `std::vector<Player*>`, never empty |
| `Args::Duration` | `30` (minutes), `30s`/`5m`/`2h`/`7d`, `0`/`perm` | `d.Value`, a `std::chrono::seconds`; `0` means permanent |
| `Args::SteamId` | numeric SteamID64 | `id.Value` |
| `Args::PlayerOrSteamId` | an online player, or a bare SteamID64 for an offline one | `who.Online` (may be null) and `who.SteamId` |
| `Args::Int` | integer | `n.Value` |
| `Args::U64` | non-negative 64-bit id (a workshop id) | `n.Value` |
| `Args::Word` | one verbatim token | `w.Value` |
| `Args::Rest` | the remainder of the line | `r.Value` |
| `Args::Opt<T>` | any of the above, optionally | `o.Value`, a `std::optional<T>`; `o.ValueOr(fallback)` for the inner value |

Three rules are compile-time, and breaking one is a `static_assert` naming the signature: every
parameter after `Caller` is an `Args::` type, only trailing arguments may be `Args::Opt`, and
`Args::Rest` must be last. A generic lambda cannot be a handler - its parameter list is the
argument spec, so the types have to be written out.

The handler does not run until every required argument resolves, so `Args::Target::Value` needs
no null check. `Args::Opt<T>` is the only argument that can be absent, and its default belongs in
the handler:

```cpp
std::string reason = why.ValueOr(c.Translations.Get("reason.kickedByAdmin"));
```

`c.Translations.Get(key)` with no slot resolves the server language, which is what a reason written to the
database or announced to everyone wants; `c.Ok`, `c.Fail` and `c.Say` resolve the caller's.

## Replying

@ref VoltMod::Caller is the first parameter: `c.Player` is the player (null when the server ran
the command, which `c.IsServer()` checks), `c.Slot` their slot (-1 for the server, which is also
the server-language slot), and `c.Translations` the translation table.

| Call | Result |
| --- | --- |
| `c.Ok(key, tokens)` | succeed, replying with `key` in the caller's language |
| `c.Fail(key, tokens)` | fail, replying the same way; it is a `Result` error, not a success |
| return nothing | handled, with nothing to say (a menu, a broadcast) |
| `c.Say(key, tokens)` | send one extra line now |
| `c.SayRaw(line)` | send one already-formatted line now |
| `c.Text(key, tokens)` | the localized line, to use for something other than a reply |

A handler that returns `c.Ok` or `c.Fail` needs no return type. One that mixes them with silence
returns `Reply::Silent()` for it and declares `-> Result<Reply>`, because a lambda cannot deduce a
type from both a value and nothing.

Multi-line output is a run of `Say`/`SayRaw` followed by `Ok` or `Reply::Silent()`, so it goes
through the same reply callback as everything else:

```cpp
.Run([&app](Caller c) -> Result<Reply> {
    const auto& frozen = app.Freeze.Frozen();
    if (frozen.empty())
        return c.Ok("cmd.frozenNone");

    c.Say("cmd.frozenHeader", {{"count", std::to_string(frozen.size())}});
    for (const auto& [id, row] : frozen)
        c.SayRaw(std::format("  {} ({})", row.Name, row.SteamId));
    return Reply::Silent();
});
```

## Permissions

`Permission(...)` gates the command on `runtime.Policy.Authorize`, which asks the plugin that
publishes `IPermissions` (admin-system). While none is loaded the command is **denied**, not
allowed, and the first denial is logged. Failing open there would hand every player every command.

An empty permission skips the check. See @ref players_guide for the rest of the gate.

## Surfaces

By default only players can run a command, from chat.

| Builder call | Players, chat | Players, own console | Server console, rcon, cfg |
| --- | --- | --- | --- |
| (nothing) | yes | no | no |
| `.Anywhere()` | yes | yes | yes |
| `.ServerOnly()` | no | no | yes |

`Anywhere()` and `ServerOnly()` register a real tier1 ConCommand of the same name, so rcon, cfg
files and `ExecuteServerCommand` reach the same handler:

```cpp
runtime.Commands.Add("bhop_player")
    .Describe("Grant/revoke session bhop for a player.")
    .ServerOnly()
    .Run([this](Caller, Args::SteamId id, Args::Int on) { Grant(id.Value, on.Value != 0); });
```

Server console calls (rcon, cfg files) run the same binder and handler, print their reply to the
console, and have no caller: `c.Player` is null, `c.Slot` is -1, permissions are skipped (the
console is the server), and caller-relative selectors such as `@me` match nobody. An `.Anywhere()`
command typed in a player's own console runs as that player, exactly as from chat, and replies
in chat. A `ServerOnly()` command ignores players, so put an operator command with no
permission there.

To run another plugin's console command as a player, use
`runtime.ConVars.ExecuteClientCommand(slot, "mm_lvl")`. Nothing is echoed to chat.

## Chat dispatch and quoting

The framework takes pending menu input first, then sends `!` messages through
`HandleChatMessage`; unknown names fall through to normal chat, and every line neither took is
raised as `runtime.Players.Said`. A plugin with chat rules of its own, such as mutes or admin tags,
subscribes to that and sets `Blocked`.

A line naming a command another plugin registered never reaches this plugin's chat handling: it
goes on to that plugin, so chat filters such as admin tagging cannot swallow `!m` before its owner
sees it.

The tokenizer treats a `"quoted run"` as one token and `\"` as a literal quote, so
`!ban Bob 30 "wall bang"` is three arguments. Repeated spaces produce no empty arguments; an
explicit `""` does. Console invocations are split by the engine, which quotes the same way.

## Target selectors

`Args::Target` and `Args::Targets` understand:

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

Every candidate passes through `runtime.Policy.Authorize`. Rejected matches are removed, and an
all-immune result reports immunity rather than no match. Self-targeting is allowed. A selector
leaving several players binds to `Args::Targets` and fails on `Args::Target`.

## Usage lines and reserved keys

The usage line is built from the argument types and localized, so no English literal lives in
C++. `cmd.usage` is the frame and the `cmd.usage.` keys supply each placeholder:

```
cmd.usage                  "Usage: {usage}"
cmd.usage.target           "target"          cmd.usage.targets   "targets"
cmd.usage.duration         "duration"        cmd.usage.steamId   "steamId"
cmd.usage.playerOrSteamId  "target|steamId"  cmd.usage.int       "number"
cmd.usage.u64              "id"              cmd.usage.word      "value"
cmd.usage.rest             "reason"
```

Required arguments get angle brackets and optional ones square brackets:

```text
!ban <target> <duration> [reason]
```

The prefix comes from the surface being answered - `!` in chat, nothing in the console. `cmd.usage`
also receives `{prefix}`, `{command}` and `{args}` separately. `UsageKey("cmd.unbanUsage")`
replaces the whole line with one key of your own.

Argument failures reply from `target.noMatch`, `target.immune`, `target.ambiguous` (gets
`{count}`), `target.dead`, `target.bot`, `cmd.badDuration`, `cmd.badSteamId`, `cmd.badNumber`,
`cmd.noPermission`, `cmd.tooManyArgs`, and `cmd.usage` for arity errors. The framework ships
English defaults for all of them; your own translation file wins.

## Testing

The router reaches engine state only through `ArgBinder`, so parsing, binding, permissions,
surfaces and replies test with a stub binder. See @ref testing_guide.
