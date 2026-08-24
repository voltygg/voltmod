# Architecture {#architecture}

[TOC]

## Modules

```
VoltMod
├── Core        Primitives: policy, scheduler, slot events, subscriptions,
│               translations, parsing, colors, string/time helpers
├── Sdk         HL2SDK wrapper layer (entities, events, messages, gamedata)
├── Players     Player tracking, target selectors, action dispatch
├── Commands    Declarative chat commands (CommandSpec)
├── Menu        WASD center-HTML menus + Flow wizard
├── Database    Async PostgreSQL + row mapping (VOLTMOD_ENABLE_POSTGRES)
├── Http        Async HTTP client + JSON REST helpers
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
directly (`runtime.Messages`, not `runtime.Sdk.Messages`) so internal module
moves do not break consumers.

```cpp
runtime.Players.GetPlayerBySlot(slot);
runtime.Messages.Reply(slot, "done");
runtime.Schema().GetOffset("CCSPlayerPawn", "m_iHealth");   // Schema() is a method
```

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

## PluginPolicy

@ref VoltMod::Core::PluginPolicy is the one bridge between the framework's generic machinery and your domain rules. Set it once in OnLoad:

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
through @ref VoltMod::App::ServiceExchange instead of exposing a manager or
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
the module boundary. Use a @ref VoltMod::Sdk::ServerCommand when console, RCON,
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

## Cleanup and subscriptions

Cleanup belongs in a member destructor or a
@ref VoltMod::Core::Subscription "Subscription":

```cpp
_spawn = runtime.Events.Listen<Events::PlayerSpawn>([this](const auto& e) { OnSpawn(e.Slot); });
```

`Subscription` is move-only and unregisters on destruction, so a listener cannot
outlive captured state. `VOLTMOD_SCOPED_HOOK` provides the same lifetime for
SourceHook installs. On unload, the base runs `OnUnload`, removes its standard
hooks, and destroys the runtime.

## The frame pump

The plugin's GameFrame hook calls `Runtime::OnGameFrame()`, which ticks exactly one thing: the @ref VoltMod::Core::Scheduler. Everything per-frame (menu input, HTTP completions, database completions) registers a `Scheduler::EveryFrame` timer, so there is no hardcoded pump list to keep in sync.

## Module layering

`scripts/voltmod/modgraph.py` holds the map and enforces it. A cycle check would not be
enough: an upward edge (Core reaching into Sdk) stays acyclic and is exactly what breaks
the layering.

- **Core** depends on nothing else in the framework
- **Sdk** and **Http** sit on Core
- **Players** on Core + Sdk; **Commands** and **Menu** on Core + Sdk + Players
- **Database** is Core + libpqxx, compiled only under `VOLTMOD_ENABLE_POSTGRES`
- **App** may reach all of them; it is the composition root

`Detail/` is the one exemption: it holds the ambient pointer to the live `Runtime`.
Class templates instantiated in consumer TUs (`Flow<TState>`, `PerSlot<T>`) and
callback trampolines with no user data reach the runtime through it. Plugin code
never needs it.
