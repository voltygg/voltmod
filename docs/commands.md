# Commands {#commands_guide}

[TOC]

A command is one aggregate - name, metadata, permission, typed arguments, handler - registered at its definition site. The kit resolves and validates every argument *before* your handler runs: targets go through the selector grammar with immunity applied, durations and SteamIDs are parsed, and every failure already replied to the caller in their language. Your handler only sees the happy path.

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
        std::string name = c.Target().GetName();     // capture first - a ban can drop the target
        if (!IssueBan(*c.Caller, c.Target(), c.Reason, c.Duration().value_or(0)))
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

| Factory | Consumes | Read with |
|---------|----------|-----------|
| `Target(rules = {})` | one token via the selector grammar | `c.Target()` - a reference, never null (and `c.Targets()` when `rules.AllowMultiple`) |
| `TargetOrSteamId()` | online player, or a bare SteamID64 for offline targets | `c.HasTarget() ? c.Target() : ...` plus `c.SteamId` |
| `Duration()` | `30` (minutes), `30s`/`5m`/`2h`/`7d`, `0`/`perm` | `c.Duration()` - nullopt when absent, `0` means permanent |
| `SteamId64(errorKey = {})` | numeric SteamID64 | `c.SteamId` |
| `Int()` | integer | `c.Int()` - nullopt when absent |
| `Word(required = true)` | one verbatim token | `c.Word` |
| `ReasonTail(fallbackKey = {})` | all remaining tokens joined | `c.Reason` (the translated fallback when absent) |

Resolution refuses to run the handler unless every required argument came through, so an
accessor for an argument the spec declared always has a value. That is why `Target()` hands
back a reference: the old pointer only invited a null check that could not fire.

`Usage` is derived from the argument kinds - `!ban <target> <duration> [reason]` - unless you
set it. The prefix comes from the surface being replied to (the manager's first chat prefix, or
nothing at all in the console), so the same spec reads correctly in both places. Extra arguments
beyond what the spec consumes are refused with `cmd.tooManyArgs` rather than dropped.

`TargetRules` narrows what a Target argument accepts: `{.AllowMultiple = true}` permits `@all`-style selectors, `AllowDead`/`AllowBots` filter the match set.

`CommandContext` also carries `Caller`, `RawArgs`, and the localized result helpers `Ok(key, tokens)` / `Fail(key, tokens)` - both translate in the caller's language.

## Target selectors

The `Target` argument (and @ref VoltMod::Players::ResolveTargets directly) understands:

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

Immunity comes from `runtime.Policy.CanTarget` - matches the policy rejects are dropped, and if that empties the set the caller is told the target is immune, not "no match".

## Permissions

A spec with a non-empty `Permission` is gated on `runtime.Policy.HasPermission`. **If no
policy is installed the command is denied**, not allowed, and the kit logs an error once per
command name - a plugin that declares permissions without wiring a policy is misconfigured,
and failing open there hands every player every command.

## Error replies and reserved keys

Argument failures reply from these translation keys - ship them in your translation files:

`target.noMatch`, `target.immune`, `target.ambiguous` (gets a `{count}` token), `target.dead`, `target.bot`, `cmd.badDuration`, `cmd.badSteamId`, `cmd.badNumber`, `cmd.noPermission`, `cmd.tooManyArgs`, and the command's `Usage` (derived unless set) for arity errors. Override per-argument with `ArgSpec::ErrorKey` (e.g. `SteamId64("cmd.unbanUsage")`).

## Introspection

`runtime.Commands.GetAllCommands()` returns every registered spec with its `Name`, `Aliases`, `Description`, `Usage`, and `Permission` - enough to build a `!help` command or an admin menu from the same data the dispatcher uses.

## Console callers

`Surfaces` says where a spec can be invoked from; it defaults to `Surface::Chat`, and it gates
both ends - a spec that does not name `Surface::Chat` is not reachable from chat at all. Adding
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

Console invocations run the same argument resolution and handler with no caller, and print
their reply to the console. With no caller there is no SteamID to check, so `Permission` is
never consulted - the server console already is the authority - and caller-relative selectors
(`@me`, `@!me`) match no one. That is also why `Surface::Console` on its own matters: such a
command usually carries no `Permission`, and chat has callers that a permission check would
otherwise be the only thing standing in front of. Replies resolve translations against slot `-1`, the server
language. Registration owns the ConCommand, so `Unregister` and destruction remove it.
