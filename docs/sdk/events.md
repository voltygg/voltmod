# ConVars and game events {#sdk_events_guide}

[TOC]

## GameEvents

Prefer the typed listeners: each struct in `VoltMod::Events` (`Events/EventTypes.hpp`) carries the event name and decodes its fields for you. Available: `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerBlind`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `BulletImpact`, `RoundStart`, `RoundEnd`, `RoundPrestart`.

```cpp
namespace Events = VoltMod::Events;
auto& events = runtime.Events;

auto death = events.Listen<Events::PlayerDeath>([](const Events::PlayerDeath& e) {
    // e.VictimSlot, e.AttackerSlot, e.Headshot, e.Weapon, ...
});

// Keep `death` beside the state captured by the callback.
```

For events the framework has not modeled, use the string overload with the same
registration API and a raw `IGameEvent*`:

```cpp
events.Listen("bomb_planted", [](IGameEvent* event) {
    int site = event->GetInt("site");
});
```

You can also create and fire events (`CreateEvent` / `FireEvent` / `FreeEvent`); the center-HTML transport is built on exactly that.

### BulletImpact: correlate by tick, not identity

@ref VoltMod::Events::BulletImpact "BulletImpact" fires once per bullet landing, so one shotgun blast produces several. Its catch is that the engine truncates the event's `userid` to its low byte before sending it, so the value does not round-trip to a player: `Slot` is a best-effort decode that is `-1` whenever the truncated id names no live player, and it can name the *wrong* player when two userids share a low byte.

Attribute impacts to a shot by **tick proximity** (the impacts belonging to a `WeaponFire`, or to a usercmd carrying an attack, arrive in the same tick), and use `TruncatedUserId` only to disambiguate among candidates in that tick. Never key state on `Slot` alone.

```cpp
events.Listen<Events::BulletImpact>([&clock = runtime.Clock](const Events::BulletImpact& e) {
    Record(clock.Tick(), e.TruncatedUserId, e.X, e.Y, e.Z);
});
```

### PlayerDeath.Penetrated

`Penetrated` is the number of surfaces the killing bullet passed through; `> 0` is a wallbang. It is the field that tells a legitimate-looking kill apart from one taken through geometry the shooter could not see.

### Listener lifecycle

Call `Listen` during plugin load and keep the returned `Subscription`. The
framework handles a Source engine quirk: `AddListener` succeeds
before the first map, but the engine resets its listener table during each map
startup. The framework re-attaches every listener from its `StartupServer` hook
on every map start. Use `Attached N/N game event listener(s) at map start.` in
the server log as the health check.

Related lifecycle points:

- `MetamodPlugin::OnServerStartup(mapName)` is the plugin-facing map-start callback. The engine resets game convars and re-execs gamemode cfgs around map init, so values set at load time may need re-asserting from here or from a `RoundStart` listener. The same hook stores the map in `runtime.CurrentMap`, so a plugin that only wants to stamp the current map on a record does not need to override anything. Note that it stays empty after a late (mid-map) load until the next map change.
- On `meta reload`, `Shutdown` detaches everything (`RemoveAllListeners`), and the fresh load re-registers, so there is no double dispatch.
- A handler may `Listen` or `RemoveListener` while it runs: dispatch works from a snapshot of the handles and re-resolves each one before calling it, so the registry is free to change underneath. A listener removed by an earlier handler in the same event does not fire; one registered during it starts with the next event.

### Inspecting a client's own subscriptions

`GetClientLegacyListener(slot)` returns the engine-side listener object the game keeps for that client: the client's own subscription handle, not a framework listener. Firing an event at it delivers to that one client (this is how @ref VoltMod::Messaging::Messages "Messages" sends center HTML). It is `nullptr` when the slot has no client or when the `"LegacyGameEventListener"` gamedata signature did not resolve.

`ClientListensTo(slot, eventName)` asks the event manager whether that handle is subscribed to a given event:

```cpp
if (runtime.Events.ClientListensTo(slot, "player_death"))
    /* ... */;
```

A vanilla client subscribes only to the events its HUD needs, so a subscription it has no business holding is a fingerprint of injected client code. Both calls degrade to `nullptr`/`false` rather than failing, so `false` means "not subscribed **or** unavailable". Resolve `GetClientLegacyListener` once and check it for null if you need to tell those apart.

## ConVars

Typed reads and writes over ICvar:

```cpp
auto& cvars = runtime.ConVars;

if (auto gravity = cvars.GetFloat("sv_gravity"))   // getters return std::optional
    Use(*gravity);

cvars.SetFloat("sv_gravity", 400.0f);
cvars.ExecuteServerCommand("mp_restartgame 1");

// Global change listener; the id cancels it via RemoveChangeListener.
uint64_t id = cvars.OnChange([](const char* name, const char* oldValue, const char* newValue) {
    /* ... */
});
```

The setters change the server's stored value and fire change callbacks, but they do **not** network anything. An `FCVAR_REPLICATED` convar set this way silently diverges from what clients predict with. They also do no cross-type conversion: the SDK's `SetAs<T>` no-ops when the convar's type has no conversion from `T` (e.g. `SetInt` on a bool convar like `sv_autobunnyhopping`; `SetString` works for any type). For a server-wide change that must reach clients, use `ExecuteServerCommand("name value")`. The console path both sets and replicates, exactly as a cfg line would. Two escape hatches cover the per-player cases.

### Taking a convar over server-wide

A feature that overrides a server convar owes the operator two things: save their value before the *first* write, and restore only what it actually took. @ref VoltMod::Engine::ConVarLease "ConVarLease" holds those snapshots, and it writes through the console path so replicated convars reach clients:

```cpp
VoltMod::ConVarLease lease{runtime.ConVars};   // restores on destruction

lease.Override("sv_gravity", 250.0f);          // false if the server has no such convar
lease.Restore("sv_gravity");                   // no-op if it never took it
```

`Override` saves on the first take and re-asserts afterwards, so calling it every round is correct and necessary - the engine resets convars around a map change, and an override that is not re-asserted silently lapses. It returns `false` for a convar the server does not have, rather than recording a snapshot it could never restore. The destructor calls `RestoreAll()`, which is what makes unload safe. The admin-system fun toggles and bhop's "enabled" mode both drive one of these.

### Per-client replication

@ref VoltMod::Engine::ConVars::ReplicateToClient "ReplicateToClient" sends `CNETMsg_SetConVar` to a single client, so only that client's view of a replicated convar changes; the server value and every other client are untouched. This is how you make *one* player's prediction run with different movement settings (the bhop plugin replicates `sv_autobunnyhopping` to granted players):

```cpp
cvars.ReplicateToClient(slot, "sv_autobunnyhopping", "1");
```

The client's connect/map-change snapshot restores the server value, so re-send the override from a `PlayerSpawn` listener to keep it sticky.

### Raw value access

@ref VoltMod::Engine::ConVarStorage "Storage(name)" returns a handle to the convar's raw storage: reads and writes skip change callbacks *and* replication. Use it for scoped flips around one player's processing (e.g. inside a @ref VoltMod::Hooks::Movement "Movement" pre/post pair), where the engine setters' broadcast would leak the change to everyone. You are responsible for restoring the prior value; the handle stays valid for the convar's lifetime, so resolve once and cache.

```cpp
VoltMod::ConVarStorage autoBhop = cvars.Storage("sv_autobunnyhopping");
bool saved = autoBhop.GetBool();
autoBhop.SetBool(true);   // no callbacks, nothing networked
// ... run the per-player work ...
autoBhop.SetBool(saved);
```

## Map

@ref VoltMod::Engine::Map validates map names and changes level. It deliberately holds no
map list: which maps a server offers is operator configuration, not engine state, so the list
belongs to the plugin.

```cpp
auto& maps = runtime.Maps;

if (maps.IsValid("de_dust2"))          // filesystem probe; load-time work, not per-frame
    maps.ChangeLevel("de_dust2");

maps.ChangeToWorkshop(3070563536ull);  // workshop maps are addressed by published-file id
```

`IsValid` only answers for plain names. A workshop map is addressed by id and is not mounted
until it loads, so there is nothing to probe - check those at load by other means or accept the
engine's own failure.

Both change calls take effect immediately. A plugin that wants players to read an announcement
first should schedule the call rather than delaying inside a listener.
