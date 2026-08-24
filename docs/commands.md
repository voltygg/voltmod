# Commands {#commands_guide}

[TOC]

A command is one aggregate: name, metadata, permission, typed arguments, and a
handler. Register it from the plugin's load path. The framework resolves and
validates every argument before the handler runs, including target immunity,
duration parsing, and SteamID parsing. Failures are translated for the caller;
the handler sees only valid input.

## A complete command

```cpp
#include <VoltMod/Api.hpp>

using namespace VoltMod::Commands;

commands.Register({
    .Name = "ban",
    .Description = "Ban a player.",
    .Permission = "b",
    .Args = {Target(), Duration(), ReasonTail("reason.bannedByAdmin")},
    .Handler = [](CommandContext& c) {
        std::string name = c.Target().GetName();     // capture first: a ban can drop the target
        if (!IssueBan(*c.Caller, c.Target(), c.Reason, c.Duration().value_or(0)))
            return c.Fail("cmd.banFailed");
        return c.Ok("cmd.banSuccess", {{"name", name}});
    },
});
```

After `OnLoad`, the framework records the registered command count in the load
report. The default `OnPlayerChat` dispatches `!`
and `.` messages through `HandleChatMessage`; unknown names fall through to
normal chat. A plugin with its own chat service can override `OnPlayerChat` and
take over dispatch.

## The pipeline

For each chat command, the manager matches the prefix, checks
`runtime.Policy.HasPermission(callerSteamId, spec.Permission)`, resolves and
validates each argument, calls the handler, and routes its `CommandResult.Message`
through `runtime.Policy.Reply` (or `runtime.Messages.Reply` when no policy reply
is installed). An empty `Permission` skips the permission check. Any failure
replies with a localized message and stops before the handler.

## Argument kinds

Declare `Args` with the terse factories; each fills a `CommandContext` field:

| Factory | Consumes | Read with |
|---------|----------|-----------|
| `Target(rules = {})` | one token via the selector grammar | `c.Target()`, a reference and never null (`c.Targets()` when `rules.AllowMultiple`) |
| `TargetOrSteamId()` | online player, or a bare SteamID64 for offline targets | `c.HasTarget() ? c.Target() : ...` plus `c.SteamId` |
| `Duration()` | `30` (minutes), `30s`/`5m`/`2h`/`7d`, `0`/`perm` | `c.Duration()`, nullopt when absent; `0` means permanent |
| `SteamId64(errorKey = {})` | numeric SteamID64 | `c.SteamId` |
| `Int()` | integer | `c.Int()`, nullopt when absent |
| `Word(required = true)` | one verbatim token | `c.Word` |
| `ReasonTail(fallbackKey = {})` | all remaining tokens joined | `c.Reason` (the translated fallback when absent) |

The manager does not run the handler until every required argument resolves. An
accessor for an argument declared by the spec therefore has a value. `Target()`
returns a reference for the same reason; callers do not need a null check that
cannot succeed.

`Usage` is derived from the argument kinds, for example
`!ban <target> <duration> [reason]`, unless you set it. The prefix comes from the surface being replied to (the manager's first chat prefix, or
nothing at all in the console), so the same spec reads correctly in both places. Extra arguments
beyond what the spec consumes are refused with `cmd.tooManyArgs` rather than dropped.

`TargetRules` narrows what a Target argument accepts: `{.AllowMultiple = true}` permits `@all`-style selectors, `AllowDead`/`AllowBots` filter the match set.

`CommandContext` also carries `Caller`, `RawArgs`, and the localized result helpers `Ok(key, tokens)` / `Fail(key, tokens)`; both translate in the caller's language.

## Target selectors

The `Target` argument (and @ref VoltMod::Players::ResolveTargets directly) understands:

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

Immunity comes from `runtime.Policy.CanTarget`. Matches rejected by the policy
are removed; if none remain, the caller is told that the target is immune rather
than receiving a "no match" error.

## Permissions

A spec with a non-empty `Permission` is gated on `runtime.Policy.HasPermission`. **If no
policy is installed the command is denied**, not allowed, and the framework logs an error once per
command name. A plugin that declares permissions without wiring a policy is misconfigured,
and failing open there hands every player every command.

## Error replies and reserved keys

Argument failures reply from these translation keys. Ship them in your translation files:

`target.noMatch`, `target.immune`, `target.ambiguous` (gets a `{count}` token), `target.dead`, `target.bot`, `cmd.badDuration`, `cmd.badSteamId`, `cmd.badNumber`, `cmd.noPermission`, `cmd.tooManyArgs`, and the command's `Usage` (derived unless set) for arity errors. Override per-argument with `ArgSpec::ErrorKey` (e.g. `SteamId64("cmd.unbanUsage")`).

## Introspection

`runtime.Commands.GetAllCommands()` returns every registered spec with its `Name`, `Aliases`, `Description`, `Usage`, and `Permission`, enough to build a `!help` command or an admin menu from the same data the dispatcher uses.

## Console callers

`Surfaces` says where a spec can be invoked from; it defaults to `Surface::Chat`, and it gates
both ends: a spec that does not name `Surface::Chat` is not reachable from chat at all. Adding
`Surface::Console` registers a real tier1 ConCommand of the same name, so rcon, cfg files and
`ExecuteServerCommand` reach the same handler. Name both to get both:

```cpp
commands.Register({
    .Name = "bhop_player",
    .Description = "Grant or revoke bhop for a SteamID64.",
    .Args = {SteamId64(), Int()},
    .Surfaces = Surface::Chat | Surface::Console,
    .Handler = [&](CommandContext& c) { ... },
});
```

Console calls use the same argument resolver and handler, print replies to the
console, and have no caller. They skip `Permission`, and caller-relative
selectors such as `@me` match nobody. Translations use slot `-1`, the server
language. Unregistering or destroying the registration removes the ConCommand.
