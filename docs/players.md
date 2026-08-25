# Players, targeting, and actions {#players_guide}

[TOC]

`VoltMod::Players` tracks connected players and turns "who does this act on?"
into data: a selector grammar for command targets and policy-checked
`Action`/effect descriptors for the operation itself.

`Player` stores identity and connection metadata only. Keep admin flags,
punishments, statistics, and other plugin state in managers keyed by SteamID.

## Tracking

With @ref VoltMod::App::MetamodPlugin this is automatic: the base adds/removes players around your `OnPlayerConnect(Player*)` / `OnPlayerDisconnect(Player*)` overrides. Look players up through `runtime.Players`:

```cpp
auto* p = runtime.Players.GetPlayerBySlot(slot);        // O(1)
auto* q = runtime.Players.GetPlayerBySteamId(steamId);  // O(1)
for (auto* each : runtime.Players.GetAllPlayers()) { /* ... */ }
```

`Player` carries `GetSlot()`, `GetSteamID()`, `GetName()`, `GetIpAddress()`, `GetPlaytime()`, and `IsBot()`. For typed engine operations, build a @ref VoltMod::Sdk::PlayerController from the player's slot:

```cpp
auto controller = runtime.Entities.Controller(player->GetSlot());
controller.Slay();
int hp = controller.GetHealth();
```

**Pointer lifetime:** a `Player*` is owned by the manager and dies on disconnect, slot reuse, or `Clear()`. Never store one across the disconnect callback. Store the SteamID instead.

### Per-slot plugin state

Plugin state keyed by slot has one recurring bug: values leaking from a disconnected player to the next occupant of the slot. @ref VoltMod::Players::PerSlot solves it once: a `std::array<T, MaxPlayers>` whose entries value-reset whenever a player joins or leaves the slot (backed by `runtime.Slots`, which fires on AddPlayer/RemovePlayer/Clear):

```cpp
struct MyState { int Combo = 0; float Score = 0; };

VoltMod::PerSlot<MyState> _state;   // manager member; inert until bound
_state.BindReset(runtime.Slots);   // in the owner's ctor or Initialize()
_state[slot].Combo++;              // plain indexed access afterwards
```

`BindReset` is idempotent, `Unbind()` stops the resets without clearing the values, and the destructor unsubscribes - so a `PerSlot` may outlive nothing and still leave the feed clean. It takes the @ref VoltMod::Core::SlotEvents feed rather than the runtime, so a translation unit that includes only `PerSlot.hpp` still compiles.

For time-decaying per-player scores (suspicion, rate limits), pair it with @ref VoltMod::Core::DecayingScore, a clock-free accumulator that drains linearly between caller-supplied timestamps, or @ref VoltMod::Core::SlidingWindowScore when the threshold is "N events in the last M seconds" and evidence should expire on a hard boundary instead of fading. Both take caller-supplied seconds; @ref VoltMod::Core::TimeUtils::MonotonicSeconds is the matching clock. Angle bookkeeping helpers (`NormalizeAngleDelta`, `AnglesToPoint`, `AngularDistance`) live in `VoltMod::AngleMath` (`<VoltMod/Core/AngleMath.hpp>`); note its `AngularDistance` is a Euclidean pitch/yaw metric, not a great-circle angle, so it under-reports near the poles. Consumers that need true angular separation should build it from `AnglesToPoint` and a dot product. All are unit-tested in the framework's SDK-free test suite.

## Resolve targets

@ref VoltMod::Players::ResolveTargets resolves one token to players against the runtime's roster, applying the immunity policy (`runtime.Policy.CanTarget` unless you pass your own):

```
@all @*        everyone                @me    yourself        @!me   everyone else
@t @ct @spec   by team                 @dead  @alive          @bot   @human
@random        one random player       @randomt  @randomct    one random per team
#3             slot index              765611...  STEAM_...  [U:1:...]   SteamIDs
name           exact match, then prefix, then substring (case-insensitive)
```

```cpp
using namespace VoltMod::Players;

auto result = ResolveTargets(runtime, token, caller, {.AllowMultiple = true, .AllowBots = false});
if (!result)
{
    switch (result.error().Error)
    {
    case TargetError::NoMatch:    /* "no player matched" */ break;
    case TargetError::Immune:     /* matches existed; policy blocked them all */ break;
    case TargetError::Ambiguous:  /* result.error().Count matches; narrow the token */ break;
    case TargetError::MultiNotAllowed:
    case TargetError::DeadNotAllowed:
    case TargetError::BotNotAllowed: /* rules rejected the match */ break;
    }
    return;
}
for (Player* target : *result) { /* ... */ }
```

`TargetRules` says what the call site accepts: `AllowMultiple` permits `@all`-class selectors, `AllowDead`/`AllowBots` filter. A single-target call gets exactly one player or a failure, never a silent first-of-many. Error *text* stays with you (translation keys); the framework returns the typed reason. Command `Target()` arguments run this same resolution and reply from the reserved keys automatically (@ref commands_guide).

The grammar core is engine-free (`Targeting.hpp`: `ParseTargetToken` + `FilterRoster` over plain `PlayerView` records), which is what makes it unit-testable; the framework's own tests cover it without a server.

## Actions

An @ref VoltMod::Players::Action is a single-target operation as data: permission token, guards, body. The @ref VoltMod::Players::ActionDispatcher owns the resolve → permission → immunity → run → broadcast pipeline, reading everything from `runtime.Policy`. It holds nothing but the runtime reference, so build one where you need it:

```cpp
using namespace VoltMod::Players;

const Action Slay{"s", /*RequireAlive=*/true, [](const ActionContext& ctx) -> OptKey {
    ctx.TargetCtrl.Slay();
    return "broadcast.slain";     // policy Broadcast sink announces it; nullopt = silent
}};

ActionDispatcher{runtime}.Run(adminSlot, targetSlot, Slay);
```

`ActionContext` carries the resolved `Caller`/`Target` players plus transient `CallerCtrl`/`TargetCtrl` controllers. `ParamAction` adds an int the call site supplies (health value, team id). An empty permission string skips that check.

Actions plug directly into menu context rows (`AddActionRow`, `AddStateToggleRow`, `AddPresetChoiceRow`; see @ref menus_guide), so the same data drives commands, menus, and bespoke call sites.

## Effect descriptors

Effects are toggleable or timed per-player states: the fun-command family (ghost, disco, wallhack, custom models). An @ref VoltMod::Core::EffectDescriptor declares the whole thing: permission, id, label key, broadcast keys, lifetime policy, and a `Setup` body that returns the `OnTick`/`OnStop` closures @ref VoltMod::Core::EffectManager drives:

```cpp
using namespace VoltMod;

inline const EffectDescriptor Ghost{
    .Permission = "g",
    .Id = EffectId::Ghost,
    .NameKey = "effect.ghost",
    .OnKey = "broadcast.ghosted", .OffKey = "broadcast.unghosted",
    .Scope = EffectScope::Persistent,      // or Round: auto-cancel on round end
    .Setup = [](const Players::ActionContext& ctx) -> EffectInstance {
        int slot = ctx.Target->GetSlot();
        // ActionContext carries the runtime, so an effect body needs no ambient lookup.
        auto& transmit = ctx.Rt.Transmit;
        transmit.SetPawnHidden(slot, true);
        return {.OnStop = [&transmit, slot] { transmit.SetPawnHidden(slot, false); }};
    },
};

// The menu that renders the list reads an explicit table (EffectEntry is your own
// {Order, descriptor*} record), so the order is visible in one place:
inline constexpr std::array MenuEffects{
    EffectEntry{.Order = 10, .Toggle = &Ghost},
    EffectEntry{.Order = 20, .Toggle = &Disco},
};
```

`OnStop` outlives the `ActionContext` that produced it, so capture the service, never
`ctx`. Capturing a runtime service by reference is safe because `EffectManager` is a
member of your `App`, which is destroyed before the `Runtime`.

Dispatch through an `EffectDispatcher`, which your `App` owns next to its `EffectManager`
(`EffectDispatcher PlayerEffects{runtime, Effects};`) and binds both for the load cycle:
`PlayerEffects.Toggle(adminSlot, targetSlot, descriptor)` plus its `Apply` / `Clear` siblings
(they resolve the pair and apply `runtime.Policy` first). Or drop the descriptor
straight into a menu with `AddEffectToggleRow`. `ParamEffectDescriptor` adds a `Choices` list and a parameterized `Setup` for picker-style effects (model selection); `AddEffectPickerRow` renders it. `EffectManager` guarantees `OnStop` runs exactly once however the effect ends, whether by toggle, death, disconnect, round end, or unload.

Sweeps come in three shapes: `CancelAllForSlot(slot)` clears a player, `CancelRoundScoped()` clears round-scoped effects everywhere, and `CancelPerLife(slot)` clears a player's per-life effects on death while keeping `EffectScope::Session` grants. Declare `Scope = EffectScope::Session` on the descriptor and the death sweep skips it, without any per-effect special-casing.
