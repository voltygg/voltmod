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
p->Controller().Kick("bye");                     // frame-local wrapper, used and dropped
```

Never store a `Player*` across a callback boundary, and never store a wrapper at all; the wrapper
contract is in @ref sdk_players_guide "Entities and players".

`Player` is identity only: `Slot()`, `SteamId()`, `IsBot()`, `Ref()`, `Playtime()`, `Ip()`
(captured at connect, the one moment the engine offers it) and `Name()`. `Name()` reads the
controller on every call, so a player who renames mid-match reads back renamed; it falls back to
the connect-time name only while there is no controller. `Controller()` and `Pawn()` resolve the
frame-local wrappers (`player->Pawn().Slay()`). Keep admin flags, punishments and statistics in
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

Five @ref VoltMod::Event members, in the order a connection sees them. Subscribe in the constructor
of the class whose state the handler touches, and keep each `Subscription` there:

```cpp
_joining   = runtime.Players.Connecting += [this](VoltMod::ConnectRequest& r) { r.Rejected = IsBanned(r.SteamId); };
_connected = runtime.Players.Connected += [this](VoltMod::Player& p) { RecordConnect(p.SteamId()); };
_fully     = runtime.Players.FullyConnected += [this](VoltMod::Player& p) { Baseline(p.Slot(), p.Name()); };
_settings  = runtime.Players.SettingsChanged += [this](VoltMod::Player& p) { CheckRename(p); };
_left      = runtime.Players.Disconnected += [this](VoltMod::Player& p) { FlushSession(p.SteamId()); };
```

| Event | When |
| --- | --- |
| `Connecting` | before the engine admits the player, and before the roster has them; set `Rejected` and `Reason` on the @ref VoltMod::ConnectRequest to keep them out, and they see the reason. The first plugin to refuse ends it |
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

VoltMod::PerSlot<MyState> _state{runtime.Slots};   // manager member, reset per slot
_state[slot].Combo++;                              // plain indexed access
```

The destructor unsubscribes. The constructor takes the @ref VoltMod::SlotEvents feed rather than
the runtime, so a translation unit including only `PerSlot.hpp` still compiles. A default-constructed
`PerSlot` never resets on its own.

Subscribe to `runtime.Slots.Changed` when a slot change must do more than reset a value - close a
menu, cancel a timer. It fires for additions, removals and tracked slots cleared during unload. Use
it for identity-free slot state; use the roster events when the handler needs a `Player`.

For time-decaying per-player scores (suspicion, rate limits) use @ref VoltMod::DecayingScore, which
halves a score every half-life and holds one value and one timestamp whatever the event count;
@ref VoltMod::Time::MonotonicSeconds is the matching clock. `VoltMod::RandomIndex` in
`<VoltMod/Core/Random.hpp>` is the single source of randomness - `@random` targeting uses it.

## The gate

@ref VoltMod::Policy::Authorize is the one gate between framework dispatch and a plugin's
permission and immunity rules. The runtime sets `HasPermission` to ask the plugin that publishes
@ref VoltMod::IPermissions (admin-system), so `.Permission("x")` works in every plugin while it is
loaded and denies while it is not. A plugin that enforces targeting or replies fills `CanTarget`
and `Reply` once in `Load`.

```cpp
Result<Authorized> Authorize(PlayerRef caller, std::optional<PlayerRef> target,
                             std::string_view permission) const;
```

| Condition | Result |
| --- | --- |
| `caller` is not connected (gone, or the slot changed hands) | `ErrorCode::NotFound`, no `Key` |
| `target` given but not connected | `ErrorCode::NotFound`, `Key` `target.noMatch` |
| `HasPermission` says no, or nothing publishes `IPermissions` (logged once) | `ErrorCode::Denied`, `Key` `cmd.noPermission` |
| `CanTarget` says no | `ErrorCode::Immune`, `Key` `target.immune` |
| otherwise | @ref VoltMod::Authorized - `Caller` and a maybe-null `Target` |

```cpp
auto who = runtime.Policy.Authorize(callerRef, targetRef, "b");
if (!who)
{
    runtime.Messages.SendKey(callerSlot, who.error().Key);
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

`Policy::Reply` delivers a command result line; unset, it falls back to `runtime.Messages.Send`.

## Target selectors

A `Args::Target` or `Args::Targets` command argument understands `@all`, `@me`, `@t`, `@random`,
`#slot`, SteamIDs and name fragments; the grammar and its reply keys are in @ref commands_guide.
Resolution runs inside command dispatch, applies this same gate per candidate, and is not public
API - declare the argument rather than resolving tokens yourself.
