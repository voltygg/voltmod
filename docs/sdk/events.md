# ConVars and game events {#sdk_events_guide}

[TOC]

## GameEvents

Subscribe with `On<T>`, where `T` is one of the structs in `VoltMod` (`VoltMod/Events/EventTypes.hpp`). Each carries the event name and decodes its fields for you. Available: `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerBlind`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `BulletImpact`, `RoundStart`, `RoundEnd`, `RoundPrestart`, `VoteCast`.

```cpp
using VoltMod::PlayerDeath;

auto& events = runtime.GameEvents;

auto death = events.On<PlayerDeath>([](const PlayerDeath& e) {
    // e.VictimSlot, e.AttackerSlot, e.Headshot, e.Weapon, ...
});

// Keep `death` beside the state captured by the handler.
```

There is no string form. Add new event types to `EventTypes.hpp` with a `Name`, fields, and a
`From(IGameEvent&)` decoder:

```cpp
struct BombPlanted
{
    static constexpr const char* Name = "bomb_planted";
    int Slot = -1;
    int Site = 0;
    static BombPlanted From(IGameEvent& e);
};
```

You can also create and fire events (`CreateEvent` / `FireEvent` / `FreeEvent`); the center-HTML transport is built on exactly that.

### BulletImpact: correlate by tick, not identity

@ref VoltMod::BulletImpact fires once per bullet landing. The engine truncates `userid` to one byte,
so `Slot` is best effort and may be `-1` or identify the wrong player.

Correlate impacts by tick and use `TruncatedUserId` only to disambiguate candidates. Never key state
on `Slot` alone.

```cpp
events.On<VoltMod::BulletImpact>([&clock = runtime.Clock](const VoltMod::BulletImpact& e) {
    Record(clock.Tick(), e.TruncatedUserId, e.X, e.Y, e.Z);
});
```

### PlayerDeath.Penetrated

`Penetrated` counts surfaces crossed by the killing bullet; values above zero indicate a wallbang.

### Handler lifecycle

Call `On<T>` during plugin load and retain the returned `Subscription`. The framework reattaches
listeners after the engine resets them at map startup.

Related lifecycle points:

- `OnServerStartup(mapName)` runs at map start. Reapply convars here or on `RoundStart` if map init
  resets them. `runtime.Map.Current()` stays empty after a mid-map load until the next map.
- `meta reload` detaches old listeners before the new load registers them.
- Handlers may subscribe or unsubscribe during dispatch. New handlers start with the next event.

### Inspecting a client's own subscriptions

`GetClientLegacyListener(slot)` returns the client's engine listener, or `nullptr` when unavailable.
Targeting it sends an event to that client only.

`ClientListensTo(slot, eventName)` asks the event manager whether that handle is subscribed to a given event:

```cpp
if (runtime.GameEvents.ClientListensTo(slot, "player_death"))
    /* ... */;
```

Unexpected subscriptions can indicate injected client code. `false` also means unavailable; check
`GetClientLegacyListener` first when that distinction matters.

## ConVars

@ref VoltMod::ConVar resolves once and supports `bool`, `int`, `float`, and `std::string`. `Find`
rejects type mismatches.

```cpp
auto& cvars = runtime.ConVars;

auto gravity = cvars.Find<float>("sv_gravity");
if (!gravity)
    Log::Warn("sv_gravity unusable: {}", gravity.error().Detail);   // NotFound, or Invalid on a type mismatch
else
    gravity->Set(400.0f);            // cfg-style write; replicated convars reach clients

cvars.ExecuteServerCommand("mp_restartgame 1");

// The global engine callback exists only while Changed has subscribers.
_changes = cvars.Changed += [](const VoltMod::ConVarChange& e) {
    if (e.Name == "sv_cheats")
        /* e.OldValue, e.NewValue */;
};
```

Change fields borrow engine storage and live only during the handler. Resolve convars once and keep
their handles. Unresolved handles are falsy, read as `T{}`, and reject writes.

### Writing values

`Set(value)` queues a cfg-style console write. Callbacks fire and `FCVAR_REPLICATED` values reach
clients, so a server-wide change cannot leave client prediction using the old value.

`RawScope(value)` is the narrow exception for a temporary server-only flip. It writes storage
without callbacks or networking and restores the previous value when the scope is destroyed.

### Taking a convar over server-wide

@ref VoltMod::ConVarOverrides saves the original before the first write and restores only values it
changed:

```cpp
VoltMod::ConVarOverrides overrides{runtime.ConVars}; // restores on destruction

overrides.Set(gravity, 250.0f);                // false when the handle never resolved
overrides.Restore(gravity);                    // no-op when it never changed it
```

Later `Set` calls reapply the override without replacing the saved original. Reapply after map
resets. Destruction calls `RestoreAll()`.

### Per-client replication

`SetFor(slot, value)` changes one client's replicated view without changing the server or other
clients:

```cpp
autoBhop.SetFor(slot, true);
```

The client's connect/map-change snapshot restores the server value, so re-send the override from a
`PlayerSpawn` handler to keep it sticky.

### Scoped raw flips

`RawScope(value)` changes storage without callbacks or networking, then restores it on destruction.
Use it for a narrow server-side window:

```cpp
{
    auto flip = autoBhop.RawScope(true);   // no callbacks, nothing networked
    RunTheOneTickOfWork();
}                                          // the operator's value is back
```

For hook pairs, store the scope in a member and release it in post. The handle must outlive it.

## Map

@ref VoltMod::Map validates map names and changes level. It deliberately holds no
map list: which maps a server offers is operator configuration, not engine state, so the list
belongs to the plugin.

```cpp
auto& maps = runtime.Map;

if (maps.IsValid("de_dust2"))          // filesystem probe; load-time work, not per-frame
    maps.ChangeLevel("de_dust2");

maps.ChangeToWorkshop(3070563536ull);  // workshop maps are addressed by published-file id
```

`IsValid` only answers for plain names. A workshop map is addressed by id and is not mounted
until it loads, so there is nothing to probe. Check those at load by other means or accept the
engine's own failure.

`maps.Current()` returns the map the server is running, captured from `StartupServer`. It is
empty after a late load until the next map change, since the hook has already fired by then.

Both change calls take effect immediately. A plugin that wants players to read an announcement
first should schedule the call rather than delaying inside a listener.
