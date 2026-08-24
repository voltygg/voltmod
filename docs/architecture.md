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

These are source directories, not link units - the kit builds as two libraries
(`VoltMod::Runtime` and `VoltMod::Database`). The layering between them is real and
checked: `voltmod modgraph` fails the build if a module includes a header from a layer
it is not allowed to reach.

## Ground rules

- **Game thread only.** Metamod hooks all arrive on the main thread, and everything in the kit runs there. The two exceptions - the database worker and HTTP's pool - queue their completions and replay them on the game thread from a per-frame pump, so your callbacks never race game code.
- **No process-lifetime singletons.** Every kit service is a member of one @ref VoltMod::Runtime, constructed on Load and destroyed on Unload. State cannot survive a `meta reload`.
- **Data over glue.** Commands, effects, and menu rows are described as structs (`CommandSpec`, `EffectDescriptor`, context rows); the kit owns the resolve/check/dispatch/reply pipeline around them.
- **Policy is injected once.** The kit carries no admin model. Your plugin sets `runtime.Policy` in OnLoad, and every permission gate, immunity check, and command reply in the kit goes through it.
- **Dependencies arrive through constructors.** Nothing reaches for a global to find a collaborator. The runtime is handed to `OnLoad`; you pass on what each of your own objects needs.

## Two objects, same lifetime

**@ref VoltMod::Runtime** is the kit's services, flat. `MetamodPlugin` creates it on Load
and destroys it on Unload; members are declared in dependency order and torn down in
reverse. Every service is named directly - `runtime.Messages`, not
`runtime.Sdk.Messages` - because which source module a service lives in is the kit's
business, and mirroring it here would mean every service that moved broke its callers.

```cpp
runtime.Players.GetPlayerBySlot(slot);
runtime.Messages.Reply(slot, "done");
runtime.Schema().GetOffset("CCSPlayerPawn", "m_iHealth");   // Schema() is a method
```

**Your `App`** holds everything your plugin owns for one load cycle. Build it in
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

Reverse-declaration destruction is what makes this safe: your `App` dies before the
`Runtime`, so any subscription you hold is torn down while the service it points at is
still alive.

## PluginPolicy

@ref VoltMod::Core::PluginPolicy is the one bridge between the kit's generic machinery and your domain rules. Set it once in OnLoad:

```cpp
runtime.Policy = {
    .HasPermission = [this](int64_t steamId, const std::string& perm) { return Access.HasAnyPermission(steamId, perm); },
    .CanTarget     = [this](Player& caller, Player& target) { return Access.CanTarget(caller.GetSteamID(), target.GetSteamID()); },
    .Reply         = [this](int slot, std::string_view msg) { Chat.Reply(slot, msg); },
    .Broadcast     = [this](Player& caller, Player* target, const std::string& key) { Chat.BroadcastAction(key, ...); },
};
```

Consumers: `CommandManager` (permission gate + reply routing), the target resolver (immunity), `ActionDispatcher` and the effect dispatch helpers (permission + immunity + broadcast), context menu rows and `Flow` (row enabling, validation replies). Unset members are skipped, not crashes.

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

The kit used to offer a `Registry<T>` that let a descriptor register itself at its
definition site. It was dropped: those items are constructed during static
initialization, before Load, so their handlers could only reach dependencies through a
process-wide accessor - which is the reason such an accessor existed at all. Calling a
function costs one line and hands the handler its collaborators directly.

## Teardown

There is no deferred-cleanup stack. Anything that needs undoing is either a member
whose destructor does it, or a @ref VoltMod::Core::Subscription:

```cpp
_spawn = runtime.Events.Listen<Events::PlayerSpawn>([this](const auto& e) { OnSpawn(e.Slot); });
```

`Subscription` is move-only and unregisters on destruction, so a listener cannot outlive
the state its callback captures. `VOLTMOD_SCOPED_HOOK` yields one for SourceHook installs
too. On unload the base runs your `OnUnload`, removes its own standard hooks, then
destroys the `Runtime` - whose destructor is the kit's shutdown.

## The frame pump

The plugin's GameFrame hook calls `Runtime::OnGameFrame()`, which ticks exactly one thing: the @ref VoltMod::Core::Scheduler. Everything per-frame - menu input, HTTP completions, database completions - registers a `Scheduler::EveryFrame` timer, so there is no hardcoded pump list to keep in sync.

## Module layering

`scripts/voltmod/modgraph.py` holds the map and enforces it. A cycle check would not be
enough: an upward edge (Core reaching into Sdk) stays acyclic and is exactly what breaks
the layering.

- **Core** depends on nothing else in the kit
- **Sdk** and **Http** sit on Core
- **Players** on Core + Sdk; **Commands** and **Menu** on Core + Sdk + Players
- **Database** is Core + libpqxx, compiled only under `VOLTMOD_ENABLE_POSTGRES`
- **App** may reach all of them - it is the composition root

`Detail/` is the one exemption: it holds the ambient pointer to the live `Runtime` that
class templates instantiated in consumer TUs (`Flow<TState>`, `PerSlot<T>`) and
callback trampolines with no user data reach it through. Plugin code never needs it.

## Interface contracts

| Interface | Purpose | Required? |
|-----------|---------|-----------|
| `ILogger` | Logging backend (Info/Warn/Error) | No - a built-in `ConsoleLogger` is the default |
