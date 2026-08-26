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

An @ref VoltMod::Action is a single-target operation as data: permission token, guards, body. The @ref VoltMod::ActionDispatcher owns the authorize → run → broadcast pipeline, reading everything from `runtime.Policy`. It holds nothing but the runtime reference, so build one where you need it:

```cpp
using VoltMod::Action;
using VoltMod::ActionContext;
using VoltMod::ActionDispatcher;
using VoltMod::OptKey;

const Action Slay{"s", /*RequireAlive=*/true, [](const ActionContext& ctx) -> OptKey {
    ctx.TargetCtrl.Slay();
    return "broadcast.slain";     // policy Broadcast sink announces it; nullopt = silent
}};

ActionDispatcher{runtime}.Run(adminSlot, targetSlot, Slay);
```

`ActionContext` carries the @ref VoltMod::Authorized pair (`ctx.Caller()`, `ctx.Target()`) plus transient `CallerCtrl`/`TargetCtrl` controllers. `ParamAction` adds an int the call site supplies (health value, team id). An empty permission string skips that check.

`ActionDispatcher::Resolve` returns `Result<ActionContext>` when you need the pair without running an action - `if (!ctx) return;` and `ctx->TargetPawn()` from there.

Actions plug directly into menu context rows (`AddActionRow`, `AddStateToggleRow`, `AddPresetChoiceRow`; see @ref menus_guide), so the same data drives commands, menus, and bespoke call sites.

## Effect descriptors

Effects are toggleable or timed per-player states: the fun-command family (ghost, disco, wallhack, custom models). An @ref VoltMod::EffectDescriptor declares the whole thing: permission, id, label key, broadcast keys, lifetime policy, and a `Setup` body that returns the `OnTick`/`OnStop` closures @ref VoltMod::EffectManager drives:

```cpp
using VoltMod::EffectDescriptor;
using VoltMod::EffectInstance;
using VoltMod::EffectScope;

inline const EffectDescriptor Ghost{
    .Permission = "g",
    .Id = EffectId::Ghost,
    .NameKey = "effect.ghost",
    .OnKey = "broadcast.ghosted", .OffKey = "broadcast.unghosted",
    .Scope = EffectScope::Persistent,      // or Round: auto-cancel on round end
    .Setup = [](const VoltMod::ActionContext& ctx) -> EffectInstance {
        int slot = ctx.Target().Slot();
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
