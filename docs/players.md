# Players, targeting, and actions {#players_guide}

[TOC]

`VoltMod/Players/` tracks connections and applies the authorization gate commands, actions, menu
rows and effects all go through.

## Three identities

Pick by how long the identity must stay valid. A stored slot or wrapper can act on the wrong
player after a reconnect or a respawn.

| Type | Lives for | Use it for |
| --- | --- | --- |
| @ref VoltMod::PlayerRef (`{Slot, SteamId}`) | forever, it is a value | anything you store: a menu step, a queued database completion, a scheduled task |
| @ref VoltMod::Player `&` / `*` | one connection | the player you are working with right now; owned by `runtime.Players` |
| @ref VoltMod::Controller, @ref VoltMod::Pawn | one frame | the engine entity: name, money, team, health, position |

Resolve a stored identity at the point of use:

```cpp
VoltMod::Player* p = runtime.Players.Get(ref);   // null if the slot changed hands
if (!p) return;
p->Ctrl().Kick("bye");                           // frame-local wrapper, used and dropped
```

Never store a `Player*` across a callback boundary, and never store a wrapper at all; the wrapper
contract is in @ref sdk_players_guide "Entities and players".

`Player` is identity only: `Slot()`, `SteamId()`, `IsBot()`, `Ref()`, `Playtime()`, `Ip()`
(captured at connect, the one moment the engine offers it) and `Name()`. `Name()` reads the
controller on every call, so a player who renames mid-match reads back renamed; it falls back to
the connect-time name only while there is no controller. `Ctrl()` and `GetPawn()` resolve the
frame-local wrappers (`player->GetPawn().Slay()`). Keep admin flags, punishments and statistics in
plugin managers keyed by SteamID, not on `Player`.

## The roster

```cpp
auto* p = runtime.Players.Get(slot);                    // O(1), current occupant
auto* q = runtime.Players.Get(ref);                     // O(1), same slot AND same SteamID
auto* r = runtime.Players.BySteamId(steamId);           // O(1), humans only - bots share SteamID 0
for (auto* each : runtime.Players.All()) { /* ... */ }  // slot order, no allocation
VoltMod::PlayerRef mine = runtime.Players.RefFor(slot); // promote a slot to a storable identity
```

`All()` is a view over the roster's own vector, invalidated by the next connect or disconnect: do
not join or kick anybody while iterating it.

### Connection lifecycle

Four @ref VoltMod::Event members, in the order a connection sees them. Subscribe in `Load` and
keep each `Subscription` beside the state its handler touches:

```cpp
_connected = runtime.Players.Connected += [this](VoltMod::Player& p) { RecordConnect(p.SteamId()); };
_fully     = runtime.Players.FullyConnected += [this](VoltMod::Player& p) { Baseline(p.Slot(), p.Name()); };
_settings  = runtime.Players.SettingsChanged += [this](VoltMod::Player& p) { CheckRename(p); };
_left      = runtime.Players.Disconnected += [this](VoltMod::Player& p) { FlushSession(p.SteamId()); };
```

| Event | When |
| --- | --- |
| `Connected` | the player is in the roster; their name is not meaningful yet |
| `FullyConnected` | post `ClientFullyConnect`, the first point `Name()` and the client's replicated convars mean anything |
| `SettingsChanged` | every replicated setting change, including the burst the engine sends at connect, so debounce if you act on it |
| `Disconnected` | while the player is still in the roster, so the handler can read their identity; the `Player` is destroyed right after |

Taking over an occupied slot without a disconnect raises `Disconnected` too, and so does `Clear()`
at unload. There is no `OnPlayerConnect` virtual to override: a subscription hands you a live
`Player&` and can live on whichever object owns the state.

### Per-slot plugin state

@ref VoltMod::PerSlot value-resets an entry when a player joins or leaves, so state never leaks to
the next occupant:

```cpp
struct MyState { int Combo = 0; float Score = 0; };

VoltMod::PerSlot<MyState> _state;   // manager member; inert until bound
_state.BindReset(runtime.Slots);    // in the owner's ctor or Initialize()
_state[slot].Combo++;               // plain indexed access afterwards
```

`BindReset` is idempotent and the destructor unsubscribes. It takes the @ref VoltMod::SlotEvents
feed rather than the runtime, so a translation unit including only `PerSlot.hpp` still compiles.

Subscribe to `runtime.Slots.Changed` when a slot change must do more than reset a value - close a
menu, cancel a timer. It fires for additions, removals and tracked slots cleared during unload. Use
it for identity-free slot state; use the roster events when the handler needs a `Player`.

For time-decaying per-player scores (suspicion, rate limits) use @ref VoltMod::DecayingScore, which
halves a score every half-life and holds one value and one timestamp whatever the event count;
@ref VoltMod::Time::MonotonicSeconds is the matching clock. `VoltMod::RandomIndex` in
`<VoltMod/Core/Random.hpp>` is the single source of randomness - `@random` targeting uses it.

## The gate

@ref VoltMod::Policy::Authorize is the one gate between framework dispatch and a plugin's
permission and immunity rules. Fill the callbacks once in `Load` - see
@ref plugin_guide "Writing a plugin".

```cpp
Result<Authorized> Authorize(PlayerRef caller, std::optional<PlayerRef> target,
                             std::string_view permission) const;
```

| Condition | Result |
| --- | --- |
| `caller` is not connected (gone, or the slot changed hands) | `ErrorCode::NotFound`, no `Key` |
| `target` given but not connected | `ErrorCode::NotFound`, `Key` `target.noMatch` |
| `permission` non-empty and no `HasPermission` installed | `ErrorCode::Denied`, `Key` `cmd.noPermission`, logged once |
| `HasPermission` says no | `ErrorCode::Denied`, `Key` `cmd.noPermission` |
| `CanTarget` says no | `ErrorCode::Immune`, `Key` `target.immune` |
| otherwise | @ref VoltMod::Authorized - `Caller` and a maybe-null `Target` |

```cpp
auto who = runtime.Policy.Authorize(callerRef, targetRef, "b");
if (!who)
{
    runtime.Messages.ReplyKey(callerSlot, who.error().Key);
    return;
}
Ban(who->Target->SteamId());
```

An empty `permission` skips the permission check. Targeting yourself is always allowed and never
reaches `CanTarget`, so a plugin's `CanTarget` only answers "may this caller act on somebody else".
Denial is a value: the only way to get an `Authorized` is to have passed.

`AuthorizeSteamId(caller, targetSteamId, permission)` is the same decision for a target that may be
offline. It returns @ref VoltMod::Status, because an offline target has no `Player` to hand back.
Use it wherever a command binds `Args::PlayerOrSteamId` or a bare SteamID, rather than consulting
a plugin's own immunity table.

`Policy::Reply` delivers a command result line and `Policy::Broadcast` announces a performed
action; both are unset by default, and `Reply` then falls back to `runtime.Messages.Reply`.

## Target selectors

A `Args::Target` or `Args::Targets` command argument understands `@all`, `@me`, `@t`, `@random`,
`#slot`, SteamIDs and name fragments; the grammar and its reply keys are in @ref commands_guide.
Resolution runs inside command dispatch, applies this same gate per candidate, and is not public
API - declare the argument rather than resolving tokens yourself.

## Actions

An @ref VoltMod::Action is permission, guards and body. @ref VoltMod::ActionDispatcher authorizes,
runs and broadcasts it:

```cpp
using VoltMod::Action;
using VoltMod::ActionContext;
using VoltMod::ActionDispatcher;
using VoltMod::OptKey;

const Action Slay{"s", /*RequireAlive=*/true, [](const ActionContext& ctx) -> OptKey {
    (void)ctx.TargetPawn().Slay();   // Slay() lives on Pawn, not Controller
    return "broadcast.slain";        // the Policy::Broadcast callback announces it; nullopt = silent
}};

ActionDispatcher actions{runtime.Policy, runtime.Entities};
actions.Run(adminRef, targetRef, Slay);
```

`Run` takes @ref VoltMod::PlayerRef, not slots: a stored row or callback that outlived its player is
refused rather than retargeted at whoever holds the slot now. Turn a slot into a ref at the
boundary that first receives it. `Resolve(caller, target, permission)` returns
`Result<ActionContext>` when you want the pair without running an action.

`ActionContext` carries the @ref VoltMod::Authorized pair (`ctx.Caller()`, `ctx.Target()`), the
transient `CallerCtrl`/`TargetCtrl` controllers and their pawns (`CallerPawn()`, `TargetPawn()`) -
nothing else. A body needing an engine service beyond those reaches it through the plugin's own
`App&` it already captures. `ParamAction` adds an int the call site supplies (health value, team
id). An empty permission string skips that check.

Actions plug into menu context rows (`Action`, `StateToggle`, `Presets`; see @ref menus_guide), so
the same data drives commands, menus and bespoke call sites.

## Effects

@ref VoltMod::EffectDescriptor is a toggleable, timed or parameterized player effect as data:
permission, display keys, lifetime policy, optional `Choices`, and a `Setup` returning the
callbacks @ref VoltMod::EffectManager drives.

```cpp
using VoltMod::EffectDescriptor;
using VoltMod::EffectInstance;
using VoltMod::EffectScope;

// Descriptors are static data built before any App exists, so a body that needs an engine service
// captures a Runtime& through a small factory instead of reading it off the context.
EffectDescriptor MakeGhost(VoltMod::Runtime& runtime)
{
    return EffectDescriptor{
        .Permission = "g",
        .Id = static_cast<int>(EffectId::Ghost),   // Id is a plain int; cast your effect enum
        .NameKey = "effect.ghost",
        .OnKey = "broadcast.ghosted",
        .OffKey = "broadcast.unghosted",
        .Scope = EffectScope::Persistent,          // or Round, or Session
        .Setup = [&runtime](const VoltMod::ActionContext& ctx, int) -> EffectInstance {
            int slot = ctx.Target().Slot();
            auto& visibility = runtime.Hooks.Visibility;
            visibility.SetPawnHidden(slot, true);
            return {.OnStop = [&visibility, slot] { visibility.SetPawnHidden(slot, false); }};
        },
    };
}
```

`OnStop` outlives the `ActionContext` that produced it, so capture the service, never `ctx`.
Capturing a `Runtime&` or an `App&` by reference is safe: `EffectManager` is a member of your
`App`, which is destroyed before the `Runtime`.

`Setup` also receives a `param` - 0 for a plain toggle, an @ref VoltMod::EffectChoice's `Param`
for a picker. `TickIntervalMs` runs `OnTick` on a timer, `DurationMs` auto-expires the effect, and
an empty `OnKey`/`OffKey` suppresses that broadcast.

Hold an `EffectDispatcher` beside the `ActionDispatcher` and `EffectManager`
(`EffectDispatcher PlayerEffects{Actions, Effects};`). Its `Toggle`, `Apply` and `Clear` resolve
and authorize the pair before changing anything. `ActionRows::Effect` puts a toggle in a menu and
`ActionRows::EffectPicker` builds a submenu from `Choices`.

`EffectManager` runs `OnStop` exactly once however the effect ends. The sweeps:

| Call | Clears |
| --- | --- |
| `Cancel(slot, id)` | one effect on one player |
| `CancelAll(slot)` | every effect on one player |
| `CancelAll()` | every effect on everyone |
| `CancelRound()` | every `EffectScope::Round` effect everywhere |
| `CancelOnDeath(slot)` | a player's effects except those scoped `Session` |
