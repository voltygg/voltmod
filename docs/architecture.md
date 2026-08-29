# Architecture {#architecture}

[TOC]

## Modules

```
VoltMod
├── Core        Primitives: events and subscriptions, results, capabilities, policy,
│               scheduler, slot events, translations, parsing, per-slot caches, helpers
├── Engine      Interfaces, gamedata + typed Bindings, ConVar<T>, clock, maps, precache, commands
├── Entities    Entity lookup, the Entity/Pawn/Controller wrappers, schema fields, items,
│               pawn operations
├── Events      The game event service and its typed event structs
├── Messaging   Chat and center-HTML messages, chat colors, the vote panel
├── Players     The roster, the Policy gate, action and effect dispatch
├── Hooks       Movement, transmit, teleport, chat input, client convars
├── Ui          Panorama custom_hud_layout panels and the button presses they send back
├── Workshop    Workshop addon delivery: what connecting clients are told to download
├── Commands    Chat and console commands: the fluent builder, typed Args, the router
├── Menu        Menu model and Flow wizard, with center-HTML and Panorama drivers
├── Database    Async PostgreSQL + row mapping (VOLTMOD_ENABLE_POSTGRES)
├── Http        Async HTTP client + JSON REST helpers
├── Unsafe      Opt-in raw hooking: VOLTMOD_SCOPED_HOOK (HookMacros.hpp) and
│               VOLTMOD_VHOOK + VtableHook (VtableHook.hpp)
└── App         The composition root: Runtime, MetamodPlugin, ServiceExchange
```

These are source layers, not separate link units. The framework exposes
`VoltMod::Runtime` and the optional `VoltMod::Database` library. `voltmod
modgraph` rejects dependencies outside the allowed layer graph.

## Design rules

- **Game thread only.** Metamod hooks arrive on the main thread, and framework
  code runs there. The database worker and HTTP pool are the exceptions: they
  queue completions and replay them on the game thread through per-frame delivery, so
  callbacks do not race game code.
- **One load-cycle lifetime.** Every service belongs to one @ref VoltMod::Runtime,
  created on load and destroyed on unload. A `meta reload` starts clean.
- **Data over glue.** Effects and menu rows are described as structs
  (`EffectDescriptor` and context rows), and a command's handler signature is its
  argument spec. The framework owns the resolve, check, dispatch, and reply
  pipeline around them.
- **Policy is injected once.** The framework has no admin model. Your plugin fills in
  `runtime.Policy` in `OnLoad`, and one gate - `Policy::Authorize` - applies it
  everywhere. Anything that declares a permission is denied when
  `HasPermission` is unset.
- **Dependencies arrive through constructors.** `OnLoad` receives the runtime;
  your objects receive only the services they need.

## Two objects, same lifetime

**@ref VoltMod::Runtime** is the framework's service container for one load
cycle. Most services are direct members. Engine operations live under
`runtime.World`, hook feeds under `runtime.Hooks`, and raw engine access under
`runtime.Unsafe`.

```cpp
runtime.Players.Get(slot);
runtime.Messages.Reply(slot, "done");
runtime.Entities.PawnOf(slot).Health = 100;
runtime.World.Precache.Add("models/props/mine.vmdl");
runtime.Hooks.Movement.Pre += [](int slot) { /* ... */ };
```

Schema offsets are not a service. A `Field` resolves its own offset once per
`(class, field)` for the **process**, not once per load, so nothing has to be
threaded through a constructor to read `m_iHealth`. See
@ref sdk_players_guide "Entities and players".

**Your `App`** holds everything the plugin owns for one load cycle. Build it in
`OnLoad` from the runtime you are given, drop it in `OnUnload`:

```cpp
struct App
{
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}

    VoltMod::Runtime& Runtime;
    ConfigManager Config;                       // declaration order == construction order
    AdminManager Admins{Db, Config};            // each member takes what it uses
    VoltMod::EffectManager Effects{Runtime.Scheduler};
};

class MyPlugin final : public VoltMod::MetamodPlugin
{
    bool OnLoad(VoltMod::Runtime& runtime) override
    {
        _app.emplace(runtime);
        return _app->Start();
    }
    void OnUnload() override { _app.reset(); }
    std::optional<App> _app;
};
```

Your `App` dies before the runtime, so its subscriptions are removed while the
services they reference are still alive.

## Policy

@ref VoltMod::Policy connects framework dispatch to the plugin's permission and
immunity rules. Configure it once during load:

```cpp
auto& policy = runtime.Policy;
policy.HasPermission = [this](int64_t steamId, std::string_view perm) { return Access.HasAnyPermission(steamId, std::string(perm)); };
policy.CanTarget     = [this](const Player& caller, const Player& target) { return Access.CanTarget(caller.SteamId(), target.SteamId()); };
policy.Reply         = [this](int slot, std::string_view msg) { Chat.Reply(slot, msg); };
policy.Broadcast     = [this](const VoltMod::Authorized& who, std::string_view key) { Chat.BroadcastAction(std::string(key), who.Caller.Name(), ...); };
```

Commands, targeting, actions, effects, and menu rows all use @ref
VoltMod::Policy::Authorize. An unset `CanTarget` allows the pair, while an unset
`HasPermission` denies anything that declares a permission. Self-targeting is
allowed before `CanTarget` runs. See @ref players_guide for the full outcome
table.

## Cross-plugin services

Plugins are separate modules and each has its own runtime. Share typed behavior
through @ref VoltMod::ServiceExchange instead of exposing a manager or
framework object directly:

```cpp
struct IBanService
{
    static constexpr const char* InterfaceName = "admin.IBanService/1";
    virtual bool IsBanned(uint64_t steamId) const = 0;

protected:
    ~IBanService() = default;
};

runtime.Exchange.Publish<IBanService>(&bans);       // provider
if (auto* bans = runtime.Exchange.Get<IBanService>()) // consumer
    Check(*bans);
```

Include a version in `InterfaceName` and change it when the vtable or parameter
meaning changes. Query at the point of use because peers may load or unload.
Do not transfer ownership, pass allocator-owned objects, or let exceptions cross
the module boundary. Use a @ref VoltMod::ServerCommand when console, RCON,
cfg files, or untyped automation also need the operation.

## Registration is explicit

Register commands, effects, and menus from `App::Start()`, where their handlers
can capture explicit dependencies:

```cpp
// src/Commands/BanCommands.cpp
void RegisterBanCommands(VoltMod::CommandManager& commands, App& app)
{
    commands.Add("ban").Permission("b").Run(
        [&app](VoltMod::Caller c, Args::Target t, Args::Duration d) -> Result<Reply> { /* ... */ });
}
```

Do not self-register during static initialization. Event and hook registrations
return a `Subscription`; keep it beside the state captured by the handler.
Commands are owned by `CommandManager` for the load cycle.

## Signals, cleanup and subscriptions

Fixed-signature signals are public @ref VoltMod::Event members subscribed with
`+=`. Game events use typed @ref VoltMod::GameEvents::On subscriptions.

```cpp
_spawn = runtime.GameEvents.On<PlayerSpawn>([this](const PlayerSpawn& e) { OnSpawn(e.Slot); });
_slots = runtime.Slots.Changed += [this](int slot) { _state.Reset(slot); };
```

Both return a move-only, `[[nodiscard]]` @ref VoltMod::Subscription
"Subscription" that unregisters on destruction. `VOLTMOD_SCOPED_HOOK` gives
SourceHook installs the same lifetime.

Expensive event sources use `Event::Lifecycle`: the first subscriber installs
the source and the last removal uninstalls it. If a hook cannot resolve, the
subscription is empty and the reason is logged. See @ref sdk_hooks_guide for
custom vtable hooks.

Operations that can fail meaningfully return `Result<T>` or @ref VoltMod::Status, an
`std::expected` over @ref VoltMod::Error - a coarse `ErrorCode`, log text in `Detail`, and a
translation key in `Key` when a player is owed a reply.

## Per-frame delivery

The GameFrame hook ticks @ref VoltMod::Scheduler. Menu input and asynchronous
HTTP or database completions register their own `EveryFrame` work with it.

## Runtime integrity

`Runtime::Start` logs `sizeof(Runtime)` once at load. Treat it as a tripwire: an
unexpected jump between builds is worth explaining. Index a fixed-size,
`MaxPlayers`-sized array only after `VoltMod::IsValidSlot`, and prefer
`PerSlot<T>`; an unchecked `[slot]` into a service that `Runtime` owns by value
corrupts a neighbouring member rather than failing.

## Module layering

`scripts/voltmod/modgraph.py` enforces the allowed edges. It rejects upward
dependencies as well as cycles.

```text
Core       -> nothing
Engine     -> Core
Entities   -> Core, Engine
Events     -> Core, Engine, Entities
Messaging  -> Core, Engine, Entities, Events
Players    -> Core, Engine, Entities
Hooks      -> Core, Engine, Entities, Events, Players, Unsafe
Ui         -> Core, Engine, Entities, Unsafe
Workshop   -> Core, Engine, Players, Unsafe
Commands   -> Core, Engine, Entities, Messaging, Players
Menu       -> Core, Engine, Entities, Messaging, Players, Hooks, Ui
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
App        -> every module
```

`Database` adds libpqxx and is compiled only with
`VOLTMOD_ENABLE_POSTGRES`. `App` is the composition root and may reach every
module. Other modules take their narrowest dependencies through constructors or
parameters; only `App` may include `Runtime.hpp` or `Api.hpp`. Header-only
templates such as `Flow<TState>` and `PerSlot<T>` also avoid the composition
root so consumer translation units stay narrow.

File-static state is reserved for engine callbacks that cannot carry user data
and for process-wide values such as the log handler, base directory, and schema
field cache. The service that owns a callback also sets and clears its static
bridge.
