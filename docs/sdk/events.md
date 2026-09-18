# ConVars and game events {#sdk_events_guide}

[TOC]

## GameEvents

Subscribe with `On<T>` and a typed event from `<VoltMod/Events/EventTypes.hpp>`. Each struct names
the engine event and decodes its fields.

```cpp
using VoltMod::PlayerDeath;

// Keep `death` beside the state the handler captures.
auto death = runtime.GameEvents.On<PlayerDeath>([](const PlayerDeath& e) {
    // e.VictimSlot, e.AttackerSlot, e.Headshot, e.Weapon, e.Penetrated, ...
});
```

There is no string subscription API. Consuming an event that is not modeled means adding its struct
to `EventTypes.hpp` first:

```cpp
struct BombPlanted
{
    static constexpr std::string_view Name = "bomb_planted";
    int Slot = -1;
    int Site = 0;
    static BombPlanted From(IGameEvent& e);
};
```

`CreateEvent` / `FireEvent` / `FreeEvent` create and fire events; the center-HTML transport is built
on exactly that.

Call `On<T>` during load and keep the returned `Subscription`. The engine resets its listener table
at map startup and the framework reattaches every listener afterwards, so nothing has to be
re-subscribed. Handlers may subscribe or unsubscribe during dispatch; a new handler starts with the
next event. `volt reload` detaches the old listeners before the new load registers them.

`PlayerDeath::Penetrated` counts surfaces the killing bullet crossed, so anything above zero is a
wallbang.

### BulletImpact: correlate by tick, not identity

@ref VoltMod::BulletImpact fires once per bullet landing, but the engine truncates `userid` to one
byte, so `Slot` is best effort and may be `-1` or name the wrong player. Correlate impacts by tick
and use `TruncatedUserId` only to disambiguate candidates.

```cpp
runtime.GameEvents.On<VoltMod::BulletImpact>([&clock = runtime.Clock](const VoltMod::BulletImpact& e) {
    Record(clock.Tick(), e.TruncatedUserId, e.X, e.Y, e.Z);
});
```

### Inspecting a client's own subscriptions

`GetClientLegacyListener(slot)` returns the client's engine-side listener, or `nullptr` when the
slot has no client or the `GetLegacyGameEventListener` signature did not resolve. Firing an event
at it delivers to that client alone.

```cpp
if (runtime.GameEvents.ClientListensTo(slot, "player_death"))
    /* ... */;
```

A vanilla client subscribes only to what its HUD needs, so unexpected subscriptions can indicate
injected client code. `false` also means unavailable, so check `GetClientLegacyListener` first when
that distinction matters.

## ConVars

@ref VoltMod::ConVar handles `bool`, `int`, `float` and `std::string`. Resolve the handle once and
keep it; handles survive map changes, and an unresolved one is falsy, reads as `T{}` and rejects
writes.

```cpp
auto& cvars = runtime.ConVars;

auto gravity = cvars.Find<float>("sv_gravity");
if (!gravity)
    Log::Warn("sv_gravity unusable: {}", gravity.error().Detail);   // NotFound, or Invalid on a type mismatch
else
    gravity->Set(400.0f);

cvars.ExecuteServerCommand("mp_restartgame 1");

// The global engine callback exists only while Changed has subscribers.
_changes = cvars.Changed += [](const VoltMod::ConVarChange& e) {
    if (e.Name == "sv_cheats")
        /* e.OldValue, e.NewValue */;
};
```

`ConVarChange`'s string views borrow engine storage and live only for the handler.

| Call | What it does |
| --- | --- |
| `Set(value)` | Queues a cfg-style console write. Callbacks fire and `FCVAR_REPLICATED` values reach clients, so client prediction cannot be left on the old value. |
| `SetFor(slot, value)` | Changes one client's replicated view, leaving the server and other clients alone. |
| `RawScope(value)` | Writes storage with no callbacks and nothing networked, restoring the previous value when the scope dies. Not available for `std::string`. |

A client's connect and map-change snapshots restore the server value, so re-send a `SetFor`
override from a `PlayerSpawn` handler to keep it sticky. For a hook pair, store the `RawScope` in a
member and release it in post; the handle must outlive the scope.

### Taking a convar over server-wide

@ref VoltMod::ConVarOverrides saves the original before the first write and restores only what it
changed:

```cpp
VoltMod::ConVarOverrides overrides{runtime.ConVars};  // RestoreAll() on destruction

overrides.Set(gravity, 250.0f);   // false when the handle never resolved
overrides.Restore(gravity);       // no-op when it never changed it
```

Later `Set` calls reapply the override without replacing the saved original. Reapply after map
resets.

## Map

@ref VoltMod::Map validates map names and changes level. It holds no map list: which maps a server
offers is operator configuration, so that list belongs to the plugin.

```cpp
auto& maps = runtime.Map;

if (maps.IsValid("de_dust2"))          // filesystem probe; load-time work, not per-frame
    maps.ChangeLevel("de_dust2");

maps.ChangeToWorkshop(3070563536ull);  // workshop maps are addressed by published-file id
```

`IsValid` answers only for plain names. A workshop map is not mounted until it loads, so there is
nothing to probe; check those by other means or accept the engine's own failure.

`maps.Current()` is the map the server is running, captured from `StartupServer`. It stays empty
after a mid-map load until the next map change. Both change calls take effect immediately, so
schedule the call rather than delaying inside a listener when players should read an announcement
first.
