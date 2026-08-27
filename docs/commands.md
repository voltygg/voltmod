# Commands {#commands_guide}

[TOC]

A command is a name, some metadata, a permission, and a handler. The handler's
parameter list *is* the argument spec: the framework reads the arity, the order,
the parsing and the usage line straight off the signature, and only calls the
handler once every argument has resolved - including target immunity, duration
parsing and SteamID parsing. Failures are localized for whoever typed the
command.

## A complete command

```cpp
#include <VoltMod/Api.hpp>

using VoltMod::Caller;
using VoltMod::Reply;
using VoltMod::Result;
namespace Args = VoltMod::Args;

commands.Add("ban")
    .Describe("Ban a player.")
    .Alias("b")
    .Permission("b")
    .Run([&app](Caller c, Args::Target t, Args::Duration d, Args::Opt<Args::Rest> why)
             -> Result<Reply> {
        std::string name = t.Value->Name();          // capture first: a ban drops the target
        std::string reason = why.Value ? why.Value->Value : c.Tr.Get("reason.bannedByAdmin");
        if (!IssueBan(*c.P, *t.Value, reason, d.Value))
            return c.Fail("cmd.banFailed");
        return c.Ok("cmd.banSuccess", {{"name", name}});
    });
```

`Run` installs the command and hands nothing back. A command lives as long as
the @ref VoltMod::CommandManager that owns it, and `MetamodPlugin` drops every
one of them before `OnUnload`, so a handler cannot outlive the plugin state it
captured. There is no way to unregister one command on its own. The builder is
single use: `Add` starts a new one.

After `OnLoad`, the framework records the registered command count in the load
report. The default `OnPlayerChat` dispatches `!` and `.` messages through
`HandleChatMessage`; unknown names fall through to normal chat. A plugin with
its own chat service can override `OnPlayerChat` and take over dispatch.

## The pipeline

For each invocation the manager matches the prefix, looks the name (or alias)
up, asks the one gate `runtime.Policy.Authorize(caller, no target, permission)`,
checks arity, binds and validates each argument, calls the handler, and routes
the reply through `runtime.Policy.Reply` (or `runtime.Messages.Reply` when no
policy reply is installed). An empty permission skips the permission check. Any
failure replies with a localized message and stops before the handler.

## Argument types

Each handler parameter after the leading @ref VoltMod::Caller is one argument:

| Parameter | Consumes | Read with |
|-----------|----------|-----------|
| `Args::Target` | one token via the selector grammar, one online player | `t.Value`, never null |
| `Args::Duration` | `30` (minutes), `30s`/`5m`/`2h`/`7d`, `0`/`perm` | `d.Value`, a `std::chrono::seconds`; `0` means permanent |
| `Args::SteamId` | numeric SteamID64 | `id.Value` |
| `Args::PlayerOrSteamId` | an online player, or a bare SteamID64 for an offline one | `who.Online` (may be null) and `who.SteamId` |
| `Args::Int` | integer | `n.Value` |
| `Args::Word` | one verbatim token | `w.Value` |
| `Args::Rest` | the remainder of the line | `r.Value` |
| `Args::Opt<T>` | any of the above, optionally | `o.Value`, a `std::optional<T>` |

Two rules are compile-time, and breaking either one is a `static_assert` naming
the signature:

- Only trailing arguments may be `Args::Opt`.
- `Args::Rest` must be last, because it swallows the remainder of the line.

The handler does not run until every required argument resolves, which is why
`Args::Target::Value` is a plain pointer nothing needs to null-check.
`Args::Opt<T>` is the only argument that can be absent, and a default belongs in
the handler:

```cpp
std::string reason = why.Value ? why.Value->Value : c.Tr.Get("reason.kickedByAdmin");
```

`c.Tr.Get(key)` with no slot resolves the server language, which is what a
reason written to the database or announced to everyone wants; `c.Ok`, `c.Fail`
and `c.Say` resolve the caller's.

## Replying

@ref VoltMod::Caller is the handler's first parameter: `c.P` is the player (null
on the console), `c.Slot` their slot (-1 for the console, which is also the
server-language slot), and `c.Tr` the translation table.

| Call | Result |
|------|--------|
| `c.Ok(key, tokens)` | succeed, replying with `key` in the caller's language |
| `c.Fail(key, tokens)` | fail, replying the same way - a `Result` error, so it cannot be mistaken for success |
| `Reply::Silent()` | handled, with nothing to say (a menu, a broadcast) |
| `c.Say(key, tokens)` | send one extra line now |
| `c.SayRaw(line)` | send one already-formatted line now |

Multi-line output is a run of `Say`/`SayRaw` lines followed by an `Ok` or a
`Reply::Silent()`, so it goes through the same reply sink as everything else:

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

## Quoting

The chat tokenizer treats a `"quoted run"` as one token and `\"` as a literal
quote, so `!ban Bob 30 "wall bang"` is three arguments, not four. Repeated
spaces produce no empty arguments; an explicit `""` does. Console invocations
are split by the engine, which quotes the same way.

## Target selectors

`Args::Target` understands:

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

Every candidate goes through the same `runtime.Policy.Authorize` gate the command
itself did, so immunity is decided in exactly one place. Matches the gate rejects
are removed; if none remain, the caller is told the target is immune rather than
that nothing matched. Targeting yourself is always allowed. A selector that
matches more than one player is an error.

## Permissions

`Permission(...)` gates the command on `runtime.Policy.Authorize`, which asks
`HasPermission`. **If no policy is installed the command is denied**, not
allowed; the framework logs an error the first time and the load report names
every affected command through `CommandsMissingPolicy()`. A plugin that declares
permissions without wiring a policy is misconfigured, and failing open there
hands every player every command.

## Surfaces

A command is typeable in chat by default.

| Builder call | Chat | Console |
|--------------|------|---------|
| (nothing) | yes | no |
| `.Console()` | yes | yes |
| `.ConsoleOnly()` | no | yes |

`Console()` registers a real tier1 ConCommand of the same name, so rcon, cfg
files and `ExecuteServerCommand` reach the same handler:

```cpp
commands.Add("bhop_player")
    .Describe("Grant/revoke session bhop for a player.")
    .ConsoleOnly()
    .Run([this](Caller, Args::SteamId id, Args::Int on) -> Result<Reply> {
        Grant(id.Value, on.Value != 0);
        return Reply::Silent();
    });
```

Console calls run the same binder and handler, print their reply to the console,
and have no caller: `c.P` is null, `c.Slot` is -1, permissions are skipped
(the console is the server itself), and caller-relative selectors such as `@me`
match nobody. An operator command with no permission belongs on `ConsoleOnly()`,
because a command that does not name the chat surface is not typeable in chat at
all.

## Usage lines and reserved keys

The usage line is built from the argument types and localized, so no English
literal lives in C++. `cmd.usage` is the frame and `cmd.usage.<argType>` is each
placeholder:

```
cmd.usage                  "Usage: {usage}"
cmd.usage.target           "target"          cmd.usage.targets   "targets"
cmd.usage.duration         "duration"        cmd.usage.steamId   "steamId"
cmd.usage.playerOrSteamId  "target|steamId"  cmd.usage.int       "number"
cmd.usage.word             "value"           cmd.usage.rest      "reason"
```

Required arguments render as `<name>` and optional ones as `[name]`, which makes
`!ban <target> <duration> [reason]`. The prefix comes from the surface being
answered - the manager's first chat prefix, or nothing at all in the console -
so the same command reads correctly in both places. `cmd.usage` also receives
`{prefix}`, `{command}` and `{args}` separately. `UsageKey("cmd.unbanUsage")`
replaces the whole line with one key of your own.

Argument failures reply from these keys: `target.noMatch`, `target.immune`,
`target.ambiguous` (gets `{count}`), `target.dead`, `target.bot`,
`cmd.badDuration`, `cmd.badSteamId`, `cmd.badNumber`, `cmd.noPermission`,
`cmd.tooManyArgs`, and `cmd.usage` for arity errors. The framework ships English
defaults for all of them, so a plugin that forgets one gets readable English
rather than a raw key; your own translation file still wins.

## Testing

Registration and dispatch are unit-testable without a server. The router
(`src/Commands/CommandRouter.hpp`) reaches the engine only through `ArgBinder`,
so `tests/Commands/CommandRouterTests.cpp` drives the whole pipeline - tokenizer,
aliases, arity, binding, error keys, surfaces - against a stub binder, and
`tests/Commands/CommandBuilderTests.cpp` checks the descriptor a handler
signature produces plus the two compile-time signature rules, as a concept
rather than by compiling calls that must not compile.
