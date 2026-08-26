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

There is no string form. An event nobody has modeled is one nobody decodes
consistently, so consuming a new one means adding its struct to
`EventTypes.hpp` first - a `Name`, the fields you need, and a `From(IGameEvent&)`
that decodes them once:

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

@ref VoltMod::BulletImpact "BulletImpact" fires once per bullet landing, so one shotgun blast produces several. Its catch is that the engine truncates the event's `userid` to its low byte before sending it, so the value does not round-trip to a player: `Slot` is a best-effort decode that is `-1` whenever the truncated id names no live player, and it can name the *wrong* player when two userids share a low byte.

Attribute impacts to a shot by **tick proximity** (the impacts belonging to a `WeaponFire`, or to a usercmd carrying an attack, arrive in the same tick), and use `TruncatedUserId` only to disambiguate among candidates in that tick. Never key state on `Slot` alone.

```cpp
events.On<VoltMod::BulletImpact>([&clock = runtime.Clock](const VoltMod::BulletImpact& e) {
    Record(clock.Tick(), e.TruncatedUserId, e.X, e.Y, e.Z);
});
```

### PlayerDeath.Penetrated

`Penetrated` is the number of surfaces the killing bullet passed through; `> 0` is a wallbang. It is the field that tells a legitimate-looking kill apart from one taken through geometry the shooter could not see.

### Handler lifecycle

Call `On<T>` during plugin load and keep the returned `Subscription`. The
framework handles a Source engine quirk: `AddListener` succeeds
before the first map, but the engine resets its listener table during each map
startup. The framework re-attaches every listener from its `StartupServer` hook
on every map start. Use `Attached N/N game event listener(s) at map start.` in
the server log as the health check.

Related lifecycle points:

- `MetamodPlugin::OnServerStartup(mapName)` is the plugin-facing map-start callback. The engine resets game convars and re-execs gamemode cfgs around map init, so values set at load time may need re-asserting from here or from a `RoundStart` handler. The same hook stores the map in `runtime.Map`, readable back via `runtime.Map.Current()`, so a plugin that only wants to stamp the current map on a record does not need to override anything. Note that it stays empty after a late (mid-map) load until the next map change.
- On `meta reload`, `Shutdown` detaches everything (`RemoveAllListeners`), and the fresh load re-registers, so there is no double dispatch.
- A handler may subscribe or unsubscribe while it runs: dispatch works from a snapshot of the registrations and re-resolves each one before calling it, so the set is free to change underneath. A handler removed by an earlier one in the same event does not fire; one added during it starts with the next event.

### Inspecting a client's own subscriptions

`GetClientLegacyListener(slot)` returns the engine-side listener object the game keeps for that client: the client's own subscription handle, not a framework listener. Firing an event at it delivers to that one client (this is how @ref VoltMod::Messages "Messages" sends center HTML). It is `nullptr` when the slot has no client or when the `"LegacyGameEventListener"` gamedata signature did not resolve.

`ClientListensTo(slot, eventName)` asks the event manager whether that handle is subscribed to a given event:

```cpp
if (runtime.GameEvents.ClientListensTo(slot, "player_death"))
    /* ... */;
```

A vanilla client subscribes only to the events its HUD needs, so a subscription it has no business holding is a fingerprint of injected client code. Both calls degrade to `nullptr`/`false` rather than failing, so `false` means "not subscribed **or** unavailable". Resolve `GetClientLegacyListener` once and check it for null if you need to tell those apart.

## ConVars

One convar is one typed handle. @ref VoltMod::ConVar "ConVar<T>" resolves a name once and then
reads and writes without another lookup; `T` is the convar's own engine type - `bool`, `int`,
`float` or `std::string` - and `Find` refuses a convar of a different kind, so setting an `int` on
a `bool` convar is an error rather than the silent no-op it used to be.

```cpp
auto& cvars = runtime.ConVars;

auto gravity = VoltMod::ConVar<float>::Find(cvars, "sv_gravity");
if (!gravity)
    Log::Warn("sv_gravity unusable: {}", gravity.error().Detail);   // NotFound, or Invalid on a type mismatch
else
    gravity->Set(400.0f);            // SetMode::Console by default

cvars.ExecuteServerCommand("mp_restartgame 1");

// Every engine-side change. The first subscription installs ICvar's global change callback and
// dropping the last one removes it, so nothing is hooked while nobody is listening.
_changes = cvars.Changed += [](const VoltMod::ConVarChange& e) {
    if (e.Name == "sv_cheats")
        /* e.OldValue, e.NewValue */;
};
```

The three fields are `string_view`s over engine storage that live only for the duration of the
handler, so copy whatever you keep.

A registered convar outlives map changes, so resolve a handle once - in your `Start()` - and keep
it as a member. A handle that never resolved is falsy (`IsValid()`), reads as `T{}` and refuses
every write, so a server without the convar degrades rather than crashing.

### Write modes

@ref VoltMod::SetMode "SetMode" picks who finds out about the write, and that is the whole
decision:

| Mode | Callbacks | Reaches clients | Use for |
| --- | --- | --- | --- |
| `Console` (default) | yes | yes, for `FCVAR_REPLICATED` | anything server-wide |
| `Server` | yes | **no** | a value only this server reads |
| `Raw` | no | no | a flip undone in the same call stack |

`Console` is the default because it is the one that cannot half-apply: a replicated convar set any
other way leaves clients predicting the old value, and their movement or damage then disagrees with
the server.

### Taking a convar over server-wide

A feature that overrides a server convar owes the operator two things: save their value before the
*first* write, and restore only what it actually took. @ref VoltMod::ConVarLease "ConVarLease"
holds those snapshots, and writes through the console so replicated convars reach clients:

```cpp
VoltMod::ConVarLease lease{runtime.ConVars};   // restores on destruction

lease.Override(gravity, 250.0f);               // false when the handle never resolved
lease.Restore(gravity);                        // no-op when it never took it
```

`Override` saves on the first take and re-asserts afterwards, so calling it every round is correct
and necessary - the engine resets convars around a map change, and an override that is not
re-asserted silently lapses. The destructor calls `RestoreAll()`, which is what makes unload safe.
The admin-system fun toggles and bhop's "enabled" mode both drive one of these.

### Per-client replication

`SetFor(slot, value)` sends `CNETMsg_SetConVar` to a single client, so only that client's view of a
replicated convar changes; the server value and every other client are untouched. This is how you
make *one* player's prediction run with different movement settings (bhop pushes
`sv_autobunnyhopping` to granted players):

```cpp
autoBhop.SetFor(slot, true);
```

The client's connect/map-change snapshot restores the server value, so re-send the override from a
`PlayerSpawn` handler to keep it sticky.

### Scoped raw flips

`RawScope(value)` pokes the convar's value storage - no change callbacks, nothing networked - and
puts the previous value back when the returned scope dies. Use it around one player's processing
(inside a @ref VoltMod::Movement "Movement" pre/post pair), where a broadcast would leak the change
to everyone:

```cpp
{
    auto flip = autoBhop.RawScope(true);   // no callbacks, nothing networked
    RunTheOneTickOfWork();
}                                          // the operator's value is back
```

When the window is a hook pair rather than a C++ block, keep the scope in a member and drop it in
the post hook - that is what bhop's grants mode does. The scope refers to the handle, so the handle
has to outlive it.

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
until it loads, so there is nothing to probe - check those at load by other means or accept the
engine's own failure.

`maps.Current()` returns the map the server is running, captured from `StartupServer`. It is
empty after a late load until the next map change, since the hook has already fired by then.

Both change calls take effect immediately. A plugin that wants players to read an announcement
first should schedule the call rather than delaying inside a listener.
