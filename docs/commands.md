# Commands {#commands_guide}

[TOC]

A command is one aggregate - name, metadata, permission, typed arguments, handler - registered at its definition site. The kit resolves and validates every argument *before* your handler runs: targets go through the selector grammar with immunity applied, durations and SteamIDs are parsed, and every failure already replied to the caller in their language. Your handler only sees the happy path.

## A complete command

```cpp
#include <CS2Kit/Api.hpp>

using namespace CS2Kit::Commands;

commands.Register({
    .Name = "ban",
    .Description = "Ban a player.",
    .Usage = "!ban <target> <duration> [reason]",
    .Permission = "b",
    .Args = {Target(), Duration(), ReasonTail("reason.bannedByAdmin")},
    .Handler = [](CommandContext& c) {
        std::string name = c.Target->GetName();      // capture first - a ban can drop the target
        if (!IssueBan(*c.Caller, *c.Target, c.Reason, c.DurationSec))
            return c.Fail("cmd.banFailed");
        return c.Ok("cmd.banSuccess", {{"name", name}});
    },
});
```

That's the whole thing: the kit ingests every self-registered spec automatically after `OnLoad` (a "Commands" stage appears in the load report), and the default `OnPlayerChat` dispatches `!`/`.` messages through `HandleChatMessage` - unknown names fall through to normal chat. Plugins with their own chat handling (admin-style chat services) override `OnPlayerChat` and take over dispatch wholesale.

## The pipeline

For each chat command: prefix match → `runtime.Policy.HasPermission(callerSteamId, spec.Permission)` → per-argument resolve/validate → your handler → the returned `CommandResult.Message` routed through `runtime.Policy.Reply` (or a plain `runtime.Messages.Reply` line when no policy Reply is installed). An empty `Permission` skips the gate; a failure at any step replies with a localized message and never reaches the handler.

## Argument kinds

Declare `Args` with the terse factories; each fills a `CommandContext` field:

| Factory | Consumes | Fills |
|---------|----------|-------|
| `Target(rules = {})` | one token via the selector grammar | `c.Target` (and `c.Targets` when `rules.AllowMultiple`) |
| `TargetOrSteamId()` | online player, or a bare SteamID64 for offline targets | `c.Target` + `c.SteamId`, or `c.SteamId` alone |
| `Duration()` | `30` (minutes), `30s`/`5m`/`2h`/`7d`, `0`/`perm` | `c.DurationSec` (0 = permanent) |
| `SteamId64(errorKey = {})` | numeric SteamID64 | `c.SteamId` |
| `Int()` | integer | `c.IntValue` |
| `Word(required = true)` | one verbatim token | `c.Word` |
| `ReasonTail(fallbackKey = {})` | all remaining tokens joined | `c.Reason` (the translated fallback when absent) |

`TargetRules` narrows what a Target argument accepts: `{.AllowMultiple = true}` permits `@all`-style selectors, `AllowDead`/`AllowBots` filter the match set.

`CommandContext` also carries `Caller`, `RawArgs`, and the localized result helpers `Ok(key, tokens)` / `Fail(key, tokens)` - both translate in the caller's language.

## Target selectors

The `Target` argument (and @ref CS2Kit::Players::ResolveTargets directly) understands:

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

Immunity comes from `runtime.Policy.CanTarget` - matches the policy rejects are dropped, and if that empties the set the caller is told the target is immune, not "no match".

## Error replies and reserved keys

Argument failures reply from these translation keys - ship them in your translation files:

`target.noMatch`, `target.immune`, `target.ambiguous` (gets a `{count}` token), `target.dead`, `target.bot`, `cmd.badDuration`, `cmd.badSteamId`, and the command's own `Usage` string for arity errors. Override per-argument with `ArgSpec::ErrorKey` (e.g. `SteamId64("cmd.unbanUsage")`).

## Introspection

`runtime.Commands.GetAllCommands()` returns every registered spec with its `Name`, `Aliases`, `Description`, `Usage`, and `Permission` - enough to build a `!help` command or an admin menu from the same data the dispatcher uses.

## Console callers

Only chat messages are dispatched. Server-console input doesn't go through `CommandManager`; slot `-1` replies are dropped harmlessly by the message layer.
