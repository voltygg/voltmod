# Players, targeting, and actions {#players_guide}

[TOC]

`VoltMod/Players/` is who is on the server and who may act on whom: one roster,
one identity type, and one gate (@ref VoltMod::Policy::Authorize) that every
command, action, menu row and effect goes through.

`Player` stores identity only. Keep admin flags, punishments, statistics, and
other plugin state in managers keyed by SteamID.

## Three identities

A player is named three different ways, and picking the wrong one is the classic
source of "it acted on whoever took that seat next".

| Type | Lives for | Use it for |
| ---- | --------- | ---------- |
| @ref VoltMod::PlayerRef (`{Slot, SteamId}`) | forever - it is a value | anything you **store**: a menu step, a queued database completion, a scheduled task |
| @ref VoltMod::Player `&` / `*` | one connection | the player you are working with **right now**; owned by `runtime.Players` |
| @ref VoltMod::Controller, @ref VoltMod::Pawn | one frame | the engine entity: name, money, team, health, position |

Resolving goes one way down that table and is re-done each time:

```cpp
VoltMod::Player* p = runtime.Players.Get(ref);   // null if the slot changed hands
if (!p) return;
p->Ctrl().Kick("bye");                           // frame-local wrapper, used and dropped
```

Never store a `Player*` across a callback boundary, and never store a wrapper at
all; see @ref sdk_players_guide "Entities and players" for the wrapper contract.

## The roster

@ref VoltMod::MetamodPlugin keeps `runtime.Players` in step with the engine. Look
players up through it:

```cpp
auto* p = runtime.Players.Get(slot);                 // O(1), current occupant
auto* q = runtime.Players.Get(ref);                  // O(1), same slot AND same SteamID
auto* r = runtime.Players.BySteamId(steamId);        // O(1), humans only - bots share SteamID 0
for (auto* each : runtime.Players.All()) { /* ... */ }  // slot order, no allocation
VoltMod::PlayerRef mine = runtime.Players.RefFor(slot); // promote a slot to a storable identity
```

`All()` is a view over the roster's own vector, so it is invalidated by the next
connect or disconnect: do not join or kick anybody while iterating it.

@ref VoltMod::Player carries `Slot()`, `SteamId()`, `IsBot()`, `Ref()`,
`Playtime()`, `Ip()` (captured at connect, the one moment the engine offers it),
and `Name()`. `Name()` reads the controller on every call - a player who renames
mid-match reads back renamed - and falls back to the connect-time name only
while there is no controller yet. `Ctrl()` and `GetPawn()` return the frame-local
wrappers for that player:

```cpp
player->GetPawn().Slay();
int hp = player->Ctrl().GetPawn().Health;
```

### Connection lifecycle

Four @ref VoltMod::Event members on the roster, in the order a connection sees
them. Subscribe in `OnLoad` and keep each `Subscription` beside the state its
handler touches:

```cpp
_connected = runtime.Players.Connected += [this](VoltMod::Player& p) { RecordConnect(p.SteamId()); };
_fully     = runtime.Players.FullyConnected += [this](VoltMod::Player& p) { Baseline(p.Slot(), p.Name()); };
_settings  = runtime.Players.SettingsChanged += [this](VoltMod::Player& p) { CheckRename(p); };
_left      = runtime.Players.Disconnected += [this](VoltMod::Player& p) { FlushSession(p.SteamId()); };
```

- `Connected` fires once the player is in the roster. Their **name is not
  meaningful yet** - the engine has not sent it. Use `FullyConnected` for that.
- `FullyConnected` is post `ClientFullyConnect`: the first point `Name()` and the
  client's replicated convars mean anything.
- `SettingsChanged` fires on every replicated setting change, including the burst
  the engine sends at connect, so debounce if you act on it.
- `Disconnected` fires while the player is **still in the roster**, so the handler
  can read their identity and flush what it keyed on them. The `Player` is
  destroyed immediately afterwards. Taking over an occupied slot without a
  disconnect ever arriving raises it too, and so does `Clear()` at unload.

There is no `OnPlayerConnect`/`OnPlayerDisconnect` virtual to override. A
subscription hands you a live `Player&` rather than a pointer that may be null,
and it can live on whichever object actually owns the state.

### Per-slot plugin state

Plugin state keyed by slot has one recurring bug: values leaking from a disconnected player to the next occupant of the slot. @ref VoltMod::PerSlot solves it once: a `std::array<T, MaxPlayers>` whose entries value-reset whenever a player joins or leaves the slot (backed by `runtime.Slots`, which fires on every roster change):

```cpp
struct MyState { int Combo = 0; float Score = 0; };

VoltMod::PerSlot<MyState> _state;   // manager member; inert until bound
_state.BindReset(runtime.Slots);   // in the owner's ctor or Initialize()
_state[slot].Combo++;              // plain indexed access afterwards
```

`BindReset` is idempotent, and the destructor unsubscribes - so a `PerSlot` may outlive nothing and still leave the feed clean. It takes the @ref VoltMod::SlotEvents feed rather than the runtime, so a translation unit that includes only `PerSlot.hpp` still compiles.

### Reacting to a slot change yourself

`runtime.Slots.Changed` is the same feed `PerSlot` binds to, and it is a plain @ref VoltMod::Event. Subscribe when you need to do more than value-reset an array - close a menu, cancel a timer, flush a session:

```cpp
_slots = runtime.Slots.Changed += [this](int slot) { CancelCapture(slot); };
```

It fires on `Add`, `Remove`, and once per tracked slot on `Clear()`, so "arrived" and "left" both reach it - a fresh occupant has nothing pending, which is what makes one signal enough for both edges. It lives in Core rather than on `PlayerManager` so services below the roster (per-slot caches, the hooks) can hear it without depending on `Player` at all. Prefer it over `Players.Connected`/`Disconnected` when all you have is per-slot state and you never look at the player; prefer the roster events when you need the identity. Keep the returned `Subscription` beside the state the handler touches.

For time-decaying per-player scores (suspicion, rate limits), use @ref VoltMod::SlidingWindowScore when the threshold is "N events in the last M seconds" and evidence should expire on a hard boundary. It takes caller-supplied seconds; @ref VoltMod::Time::MonotonicSeconds is the matching clock. @ref VoltMod::RandomIndex is the framework's single source of randomness - use it for a random pick (`@random` targeting does) rather than seeding a generator per feature or reaching for the tick counter, which repeats within a frame. Both are unit-tested in the framework's SDK-free test suite.

## The gate

@ref VoltMod::Policy is the one bridge between the framework's machinery and your
domain rules, and @ref VoltMod::Policy::Authorize is the one place those rules are
applied. Commands, actions, effects and menu rows all call it; nothing repeats its
steps.

```cpp
VoltMod::Result<VoltMod::Authorized> Authorize(PlayerRef caller,
                                               std::optional<PlayerRef> target,
                                               std::string_view permission) const;
```

| Condition | Result |
| --------- | ------ |
| `caller` is not connected (gone, or the slot changed hands) | `ErrorCode::NotFound`, no `Key` |
| `target` given but not connected | `ErrorCode::NotFound`, `Key` `target.noMatch` |
| `permission` non-empty and no `HasPermission` installed | `ErrorCode::Denied`, `Key` `cmd.noPermission`, logged once |
| `HasPermission` says no | `ErrorCode::Denied`, `Key` `cmd.noPermission` |
| `CanTarget` says no | `ErrorCode::Immune`, `Key` `target.immune` |
| otherwise | @ref VoltMod::Authorized - `Caller` and (maybe null) `Target` |

An empty `permission` skips the permission check. **Targeting yourself is always
allowed** and never reaches `CanTarget`, so a plugin's `CanTarget` only ever
answers "may this caller act on somebody else" - an immunity comparison, nothing
more. A denial is a value: nothing is nulled out to signal it, and the only way to
get an `Authorized` is to have passed.

```cpp
auto who = runtime.Policy.Authorize(callerRef, targetRef, "b");
if (!who)
{
    runtime.Messages.ReplyKey(callerSlot, who.error().Key);
    return;
}
Ban(who->Target->SteamId());
```

Install the rules once in `OnLoad` - see @ref plugin_guide "Writing a plugin".

## Target selectors

A command's `Target()` argument understands `@all`, `@me`, `@t`, `@random`,
`#slot`, SteamIDs and name fragments; the grammar and its reserved reply keys are
covered in @ref commands_guide. Resolution runs inside command dispatch, applies
this same gate per candidate, and is not public API - a plugin picks targets by
declaring the argument, not by resolving tokens itself.

## Actions

An @ref VoltMod::Action is a single-target operation as data: permission token, guards, body. The @ref VoltMod::ActionDispatcher owns the authorize → run → broadcast pipeline, reading everything from `Policy::Authorize`. It holds the three services that pipeline needs - `Policy`, `PlayerManager`, `EntitySystem` - by reference, so build one where you need it, or hold one as a long-lived member (this is what `HtmlMenuManager` does for its context rows):

```cpp
using VoltMod::Action;
using VoltMod::ActionContext;
using VoltMod::ActionDispatcher;
using VoltMod::OptKey;

const Action Slay{"s", /*RequireAlive=*/true, [](const ActionContext& ctx) -> OptKey {
    (void)ctx.TargetPawn().Slay();  // Slay() lives on Pawn, not Controller
    return "broadcast.slain";      // policy Broadcast callback announces it; nullopt = silent
}};

ActionDispatcher{runtime.Policy, runtime.Players, runtime.Entities}.Run(adminSlot, targetSlot, Slay);
```

`ActionContext` carries the @ref VoltMod::Authorized pair (`ctx.Caller()`, `ctx.Target()`) plus transient `CallerCtrl`/`TargetCtrl` controllers - nothing else. `ParamAction` adds an int the call site supplies (health value, team id). An empty permission string skips that check. A body that needs an engine service beyond the pawns/controllers here (a hook, a message, a convar) reaches it through the plugin's own `App&` it already captures, not through the context - see the effect example below, which has the identical need.

`ActionDispatcher::Resolve` returns `Result<ActionContext>` when you need the pair without running an action - `if (!ctx) return;` and `ctx->TargetPawn()` from there.

Actions plug directly into menu context rows (`Row`, `StateToggle`, `Presets`; see @ref menus_guide), so the same data drives commands, menus, and bespoke call sites.

## Effect descriptors

Effects are toggleable or timed per-player states: the fun-command family (ghost, disco, wallhack, custom models). One @ref VoltMod::EffectDescriptor covers all of it - the plain toggle and the parameterized/picker shape both use the same type: permission, id, label key, broadcast keys, lifetime policy, an optional `Choices` list, and a `Setup` body that receives a `param` (0 for a plain toggle; the picker's selected value otherwise) and returns the `OnTick`/`OnStop` closures @ref VoltMod::EffectManager drives:

```cpp
using VoltMod::EffectDescriptor;
using VoltMod::EffectInstance;
using VoltMod::EffectScope;

// Effect bodies are static data, built before any App exists, so a body that needs an engine
// service captures a Runtime& through a small factory function instead of an ActionContext member:
Effect MakeGhost(VoltMod::Runtime& runtime)
{
    return Effect{
        .Permission = "g",
        .Id = static_cast<int>(EffectId::Ghost),  // Id is a plain int; cast your effect enum
        .NameKey = "effect.ghost",
        .OnKey = "broadcast.ghosted", .OffKey = "broadcast.unghosted",
        .Scope = EffectScope::Persistent,      // or Round: auto-cancel on round end
        .Setup = [&runtime](const VoltMod::ActionContext& ctx, int) -> EffectInstance {
            int slot = ctx.Target().Slot();
            auto& transmit = runtime.Hooks.Transmit;
            transmit.SetPawnHidden(slot, true);
            return {.OnStop = [&transmit, slot] { transmit.SetPawnHidden(slot, false); }};
        },
    };
}

// Built once (e.g. as an App member, constructed with Runtime&), then the menu that renders the
// list reads an explicit table so the display order is visible in one place - array order,
// not a field on the entry:
struct EffectEntry
{
    const Effect* Toggle = nullptr;
};

const std::array<EffectEntry, 2> MenuEffects{
    EffectEntry{&descriptors.Ghost},
    EffectEntry{&descriptors.Disco},
};
```

A body whose engine services are already reachable through the call - a menu row, a command
handler - captures its plugin's `App&` (or the specific service) the same way; only a body defined
as static data before any `App` exists needs the factory-function shape above.

`OnStop` outlives the `ActionContext` that produced it, so capture the service, never `ctx`.
Capturing a `Runtime&` (or an `App&`) by reference is safe because `EffectManager` is a member of
your `App`, which is destroyed before the `Runtime`.

Dispatch through an `EffectDispatcher`, which your `App` owns next to its `ActionDispatcher` and
`EffectManager` (`EffectDispatcher PlayerEffects{Actions, Effects};`):
`PlayerEffects.Toggle(adminSlot, targetSlot, descriptor)` plus its `Apply` / `Clear` siblings (they
resolve the pair through the wrapped `ActionDispatcher` and apply `Policy::Authorize` first). Or
drop the descriptor straight into a menu with `ActionRows::Effect`. Set `Choices` to drive a
picker submenu instead of a plain toggle - `Apply`/`Toggle` then take the picker's `param` and
`ActionRows::EffectPicker` renders it, with a reset row when `ResetLabelKey` is set.
`EffectManager` guarantees `OnStop` runs exactly once however the effect ends, whether by toggle,
death, disconnect, round end, or unload.

Sweeps come in four shapes: `Cancel(slot, id)` clears one effect, `CancelAll(slot)` clears every
effect on a player, `CancelAll()` clears everyone, `CancelRound()` clears every `Round`-scoped
effect everywhere, and `CancelOnDeath(slot)` clears a player's per-life effects on death while
keeping `EffectScope::Session` grants. Declare `Scope = EffectScope::Session` on the descriptor and
the death sweep skips it, without any per-effect special-casing.
