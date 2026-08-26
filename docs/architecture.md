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
├── Players     Player tracking, target selectors, action dispatch
├── Hooks       Movement, damage, transmit, teleport, chat input, client convars
├── Commands    Declarative chat commands (CommandSpec)
├── Menu        WASD center-HTML menus + Flow wizard
├── Database    Async PostgreSQL + row mapping (VOLTMOD_ENABLE_POSTGRES)
├── Http        Async HTTP client + JSON REST helpers
├── Unsafe      Opt-in raw hooking: VOLTMOD_SCOPED_HOOK (HookMacros.hpp) and
│               VOLTMOD_VHOOK + VtableHook (VtableHook.hpp)
└── App         The composition root: Runtime, MetamodPlugin, ServiceExchange
```

These are source directories, not link units. The framework exposes the
`VoltMod::Runtime` and optional `VoltMod::Database` libraries. The layering is checked:
`voltmod modgraph` fails the build when a module includes a header from a layer
it is not allowed to reach.

## Design rules

- **Game thread only.** Metamod hooks arrive on the main thread, and framework
  code runs there. The database worker and HTTP pool are the exceptions: they
  queue completions and replay them on the game thread from a per-frame pump, so
  callbacks do not race game code.
- **One load-cycle lifetime.** Every service belongs to one @ref VoltMod::Runtime,
  created on load and destroyed on unload. A `meta reload` starts clean.
- **Data over glue.** Commands, effects, and menu rows are described as structs
  (`CommandSpec`, `EffectDescriptor`, and context rows). The framework owns the
  resolve, check, dispatch, and reply pipeline around them.
- **Policy is injected once.** The framework has no admin model. Your plugin sets
  `runtime.Policy` in `OnLoad`; permission, immunity, reply, and broadcast paths
  consult it. A command that declares a permission is denied when
  `HasPermission` is unset.
- **Dependencies arrive through constructors.** `OnLoad` receives the runtime;
  your objects receive only the services they need.

## Two objects, same lifetime

**@ref VoltMod::Runtime** is the framework's flat service container.
`MetamodPlugin` creates it on load and destroys it on unload. Services are named
directly (`runtime.Messages`, not `runtime.Messaging.Messages`) so internal module
moves do not break consumers.

```cpp
runtime.Players.GetPlayerBySlot(slot);
runtime.Messages.Reply(slot, "done");
runtime.Entities.PawnOf(slot).Health = 100;
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
    bool OnLoad(VoltMod::Runtime& runtime, bool late) override
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

@ref VoltMod::Policy is the one bridge between the framework's generic machinery and your domain rules. Set it once in OnLoad:

```cpp
runtime.Policy = {
    .HasPermission = [this](int64_t steamId, const std::string& perm) { return Access.HasAnyPermission(steamId, perm); },
    .CanTarget     = [this](Player& caller, Player& target) { return Access.CanTarget(caller.GetSteamID(), target.GetSteamID()); },
    .Reply         = [this](int slot, std::string_view msg) { Chat.Reply(slot, msg); },
    .Broadcast     = [this](Player& caller, Player* target, const std::string& key) { Chat.BroadcastAction(key, ...); },
};
```

`CommandManager`, targeting, action dispatch, effects, context menus, and `Flow`
all consult this policy. An unset reply or broadcast callback falls back or is
skipped where documented. An unset `HasPermission` is deliberately fail-closed
for commands that declare a permission.

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

Commands, effects and menu builders are registered from your `App::Start()`, by code
that already holds what the handlers need:

```cpp
// src/Commands/BanCommands.cpp
void RegisterBanCommands(VoltMod::CommandManager& commands, App& app)
{
    commands.Register({ .Name = "ban", /* ... */,
                        .Handler = [&app](const CommandContext& ctx) { /* ... */ } });
}
```

Do not self-register descriptors during static initialization. Register them
from the load path so handlers can capture their dependencies explicitly.

## Signals, cleanup and subscriptions

Every fixed-signature signal in the framework is a public @ref VoltMod::Event member, and `+=` is
the only way to subscribe to one. Game events go through @ref VoltMod::GameEvents::On, which is
keyed by a typed struct rather than a name.

```cpp
_spawn = runtime.Events.On<PlayerSpawn>([this](const PlayerSpawn& e) { OnSpawn(e.Slot); });
_slots = runtime.Slots.Changed += [this](int slot) { _state.Reset(slot); };
```

Both hand back a `[[nodiscard]]` @ref VoltMod::Subscription "Subscription". It is move-only and
unregisters on destruction, so a handler cannot outlive the state it captured; keep it as a member
beside that state. `VOLTMOD_SCOPED_HOOK` provides the same lifetime for SourceHook installs. On
unload, the base runs `OnUnload`, removes its standard hooks, and destroys the runtime.

An `Event` whose source costs something to run - a vtable hook, an engine-wide callback - carries a
`Lifecycle`: the first subscription installs it and the last one to drop removes it. That is why
`Movement`, `Damage` and `Teleport` have no `Install()` or `Enable()` to call, and why a hook that
gamedata cannot resolve refuses the subscription (an empty `Subscription`) instead of silently
never firing. The install itself is a @ref VoltMod::VtableHook "VtableHook" value paired with a
`VOLTMOD_VHOOK` declaration; @ref sdk_hooks_guide covers using the same two pieces from a plugin.

Operations that can fail meaningfully return `Result<T>` or @ref VoltMod::Status, an
`std::expected` over @ref VoltMod::Error - a coarse `ErrorCode`, log text in `Detail`, and a
translation key in `Key` when a player is owed a reply.

## The frame pump

The plugin's GameFrame hook calls `Runtime::OnGameFrame()`, which ticks exactly one thing: the @ref VoltMod::Scheduler. Everything per-frame (menu input, HTTP completions, database completions) registers a `Scheduler::EveryFrame` timer, so there is no hardcoded pump list to keep in sync.

## Runtime integrity

`Runtime::Start` logs `sizeof(Runtime)` once at load. A trailing canary member
(`_tail`, the last field declared on the class) is checked by
`Runtime::VerifyIntegrity()` every frame and once more in `~Runtime()`; a
mismatch logs `Runtime canary corrupted: <value>` exactly once. A trip means
something wrote past the end of the `Runtime` object - almost always an
unchecked `[slot]`/`[index]` into a fixed-size, `MaxPlayers`-sized array (the
kind `VoltMod::IsValidSlot` and `PerSlot<T>` guard against) rather than a
change to `Runtime`'s own layout. Treat the size log as a tripwire too: an
unexpected jump between builds is worth explaining even without a corrupted
canary.

## Module layering

`scripts/voltmod/modgraph.py` holds the map and enforces it. A cycle check would not be
enough: an upward edge (Core reaching into Engine) stays acyclic and is exactly what breaks
the layering.

```text
Core       -> nothing
Engine     -> Core
Entities   -> Core, Engine
Events     -> Core, Engine, Entities
Messaging  -> Core, Engine, Entities, Events
Players    -> Core, Engine, Entities
Hooks      -> Core, Engine, Entities, Events, Players, Unsafe
Commands   -> Core, Engine, Entities, Players, Messaging
Menu       -> Core, Engine, Entities, Players, Messaging, Hooks
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
App        -> every module
```

**Database** is Core + libpqxx, compiled only under `VOLTMOD_ENABLE_POSTGRES`. **App** is the
composition root and may reach all of them.

There is no ambient accessor for the runtime. Everything takes what it uses through a
constructor or a parameter, and `modgraph` enforces that too: a `.cpp` outside `App/`,
`Players/`, `Commands/` and `Menu/` may not include `VoltMod/Runtime.hpp` at all.

- The engine-facing modules, `Core/`, `Http/` and `Database/` never name `Runtime`. Services and value types
  (`Entity`/`Pawn`/`Controller`, `GlowVision`, `CenterHtml`, `HttpClient`, `PostgresDatabase`)
  take the sibling services they use; the free helpers that need no service at all work off the
  pawn they are handed (`PawnOps`), and the rest take theirs as a parameter (`EffectOps`).
  Where a plugin would otherwise thread services through every call, the runtime owns a small
  facade that binds them once: `Pawns` (`runtime.Pawns`, which owns slap's fall protection)
  and `Visibility` (`runtime.Visibility`, over the `GlowVision` constructor).
- `Players/`, `Commands/`, `Menu/` and `App/` may take `Runtime&`, and the runtime-owned
  services do (`CommandManager`, `MenuManager`), as do the dispatchers a plugin builds itself
  (`ActionDispatcher`, and `EffectDispatcher` over its own `EffectManager`). The header-only templates
  and plain-data types plugins instantiate (`Flow<TState>`, `PerSlot<T>`, `MenuContext`, the
  `MenuPresets` builders) still take the single narrowest service they need, so a consumer TU
  that includes one of those headers does not pull in the whole composition root.
- A file-static stands in only where no reference can be threaded, set and cleared by the
  code that owns it. Two back engine callbacks that carry no user data (the entity system
  behind `GameEntitySystem()`, the sink for the global convar change callback); the rest are
  process-wide sinks set once at load (the `Log::Sink` behind `Core::Log`, which worker threads
  write to as well, and the base directory behind `Core::AddonFile`).

Plugin code never needs any of it: `OnLoad` hands it the runtime.
