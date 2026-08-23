# ConVars & Game Events {#sdk_events_guide}

[TOC]

## GameEventService

Prefer the typed listeners: each struct in `CS2Kit::Events` (`Sdk/GameEvents.hpp`) carries the event name and decodes its fields for you. Available: `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerBlind`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `BulletImpact`, `RoundStart`, `RoundEnd`, `RoundPrestart`.

```cpp
namespace Events = CS2Kit::Events;
auto& events = Engine().Events;

uint64_t id = events.Listen<Events::PlayerDeath>([](const Events::PlayerDeath& e) {
    // e.VictimSlot, e.AttackerSlot, e.Headshot, e.Weapon, ...
});

events.RemoveListener(id);   // or leave it - Shutdown removes everything on unload
```

For events the kit hasn't modeled, the stringly overload is the escape hatch - same registration, raw `IGameEvent*`:

```cpp
events.Listen("bomb_planted", [](IGameEvent* event) {
    int site = event->GetInt("site");
});
```

You can also create and fire events (`CreateEvent` / `FireEvent` / `FreeEvent`) - the center-HTML transport is built on exactly that.

### BulletImpact: correlate by tick, not identity

@ref CS2Kit::Sdk::Events::BulletImpact "BulletImpact" fires once per bullet landing, so one shotgun blast produces several. Its catch is that the engine truncates the event's `userid` to its low byte before sending it, so the value does not round-trip to a player: `Slot` is a best-effort decode that is `-1` whenever the truncated id names no live player, and it can name the *wrong* player when two userids share a low byte.

Attribute impacts to a shot by **tick proximity** - the impacts belonging to a `WeaponFire` (or to a usercmd carrying an attack) arrive in the same tick - and use `TruncatedUserId` only to disambiguate among candidates in that tick. Never key state on `Slot` alone.

```cpp
events.Listen<Events::BulletImpact>([](const Events::BulletImpact& e) {
    Record(CS2Kit::Sdk::ServerTick(), e.TruncatedUserId, e.X, e.Y, e.Z);
});
```

### PlayerDeath.Penetrated

`Penetrated` is the number of surfaces the killing bullet passed through; `> 0` is a wallbang. It is the field that tells a legitimate-looking kill apart from one taken through geometry the shooter could not see.

### Listener lifecycle

Call `Listen` whenever you like - typically in your manager's `Initialize` during OnLoad - and the kit takes care of when the engine actually accepts the registration. The trap it handles: `IGameEventManager2::AddListener` **succeeds** before the first map, but the engine resets its listener table during every map startup, so a registration made at plugin load on a cold boot is silently dropped and the listener never fires (no error anywhere; the callback just doesn't run). The kit therefore re-attaches every listened event from its `StartupServer` hook on **every** map start - watch for `Attached N/N game event listener(s) at map start.` in the server log as the health check.

Related lifecycle points:

- `MetamodPlugin::OnServerStartup(mapName)` is the plugin-facing map-start callback. The engine resets game convars and re-execs gamemode cfgs around map init, so values set at load time may need re-asserting from here or from a `RoundStart` listener. The same hook stores the map in `Engine().CurrentMap`, so a plugin that only wants to stamp the current map on a record does not need to override anything - note it stays empty after a late (mid-map) load until the next map change.
- On `meta reload`, `Shutdown` detaches everything (`RemoveAllListeners`), and the fresh load re-registers - no double dispatch.
- A handler may `Listen` or `RemoveListener` while it runs: dispatch works from a snapshot of the handles and re-resolves each one before calling it, so the registry is free to change underneath. A listener removed by an earlier handler in the same event does not fire; one registered during it starts with the next event.

### Inspecting a client's own subscriptions

`GetClientLegacyListener(slot)` returns the engine-side listener object the game keeps for that client - the client's own subscription handle, not a kit listener. Firing an event at it delivers to that one client (this is how @ref CS2Kit::Sdk::MessageSystem "MessageSystem" sends center HTML). It is `nullptr` when the slot has no client or when the `"LegacyGameEventListener"` gamedata signature did not resolve.

`ClientListensTo(slot, eventName)` asks the event manager whether that handle is subscribed to a given event:

```cpp
if (Engine().Events.ClientListensTo(slot, "player_death"))
    /* ... */;
```

A vanilla client subscribes only to the events its HUD needs, so a subscription it has no business holding is a fingerprint of injected client code. Both calls degrade to `nullptr`/`false` rather than failing, so `false` means "not subscribed **or** unavailable" - resolve `GetClientLegacyListener` once and check it for null if you need to tell those apart.

## ConVarService

Typed reads and writes over ICvar:

```cpp
auto& cvars = Engine().ConVars;

if (auto gravity = cvars.GetFloat("sv_gravity"))   // getters return std::optional
    Use(*gravity);

cvars.SetFloat("sv_gravity", 400.0f);
cvars.ExecuteServerCommand("mp_restartgame 1");

// Global change listener; the id cancels it via RemoveChangeListener.
uint64_t id = cvars.OnChange([](const char* name, const char* oldValue, const char* newValue) {
    /* ... */
});
```

The setters change the server's stored value and fire change callbacks, but they do **not** network anything - an `FCVAR_REPLICATED` convar set this way silently diverges from what clients predict with. They also do no cross-type conversion: the SDK's `SetAs<T>` no-ops when the convar's type has no conversion from `T` (e.g. `SetInt` on a bool convar like `sv_autobunnyhopping`; `SetString` works for any type). For a server-wide change that must reach clients, use `ExecuteServerCommand("name value")` - the console path both sets and replicates, exactly as a cfg line would. Two escape hatches cover the per-player cases.

### Per-client replication

@ref CS2Kit::Sdk::ConVarService::ReplicateToClient "ReplicateToClient" sends `CNETMsg_SetConVar` to a single client, so only that client's view of a replicated convar changes - the server value and every other client are untouched. This is how you make *one* player's prediction run with different movement settings (the bhop plugin replicates `sv_autobunnyhopping` to granted players):

```cpp
cvars.ReplicateToClient(slot, "sv_autobunnyhopping", "1");
```

The client's connect/map-change snapshot restores the server value, so re-send the override from a `PlayerSpawn` listener to keep it sticky.

### Raw value access

@ref CS2Kit::Sdk::RawConVar "Raw(name)" returns a handle to the convar's raw storage: reads and writes skip change callbacks *and* replication. Use it for scoped flips around one player's processing (e.g. inside a @ref CS2Kit::Sdk::MovementHook "MovementHook" pre/post pair), where the engine setters' broadcast would leak the change to everyone. You are responsible for restoring the prior value; the handle stays valid for the convar's lifetime, so resolve once and cache.

```cpp
CS2Kit::RawConVar autoBhop = cvars.Raw("sv_autobunnyhopping");
bool saved = autoBhop.GetBool();
autoBhop.SetBool(true);   // no callbacks, nothing networked
// ... run the per-player work ...
autoBhop.SetBool(saved);
```
