# Plugin base {#plugin_guide}

[TOC]

@ref VoltMod::Plugin is the base class the host loads. It takes the host's
engine events, keeps the player roster in step, and creates one @ref
VoltMod::Runtime per load cycle, which it passes to `OnLoad`. The plugin
supplies metadata and its load-cycle object graph.

## The skeleton

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

class MyPlugin final : public VoltMod::Plugin
{
protected:
    VoltMod::PluginInfo Info() const override
    {
        // WithBuildInfo stamps Version/Date/Commit from <VoltMod/BuildInfo.hpp>.
        return VoltMod::WithBuildInfo({ .Name = "My Plugin", .Author = "me", .LogTag = "MINE" });
    }

    bool OnLoad(VoltMod::Runtime& runtime) override
    {
        _app.emplace(runtime);
        return _app->Start();
    }

    void OnUnload() override { _app.reset(); }

private:
    std::optional<MyNs::App> _app;
};

// In the .cpp: the global instance and the host's entry point in one line:
VOLTMOD_PLUGIN(MyPlugin);
```

`VOLTMOD_PLUGIN` defines the plugin instance and `VoltMod_PluginEntry`, the one
symbol the host resolves in the library, along with this module's own hook
dispatch pointer. It all comes from `Plugin.hpp`, so the plugin header needs no
additional declarations.

`<VoltMod/Api.hpp>` covers the base class, runtime, players, commands, and core
types. Add module headers only where needed:

| Need | Header |
|---|---|
| Menus (`MenuRouter`, `CenterHtmlMenu`, `PanoramaMenu`, `MenuBuilder`, row specs, `ActionRows`, `Flow`, presets) | `<VoltMod/Menu/Api.hpp>` |
| More of Entities (`EntityRef`, `Items`, `ConVar`) or Hooks (`Movement`, `Teleport`, game events) | `<VoltMod/Entities/Api.hpp>`, `<VoltMod/Hooks/Api.hpp>` |
| A JsonConfig-backed settings struct | `<VoltMod/App/Config.hpp>` (see @ref config_guide) |
| Raw interfaces, gamedata, or vtable hooking | `<VoltMod/Unsafe/Api.hpp>` |
| Database (Postgres/MariaDB/SQLite) | `<VoltMod/Database/Api.hpp>` (see @ref database_guide) |

See @ref getting_started "Getting started" for the full table.

`App` owns plugin state for one load cycle. It receives the runtime and passes
each member only the services it needs. Members initialize in declaration order,
so an initializer may reference only members declared above it.

```cpp
struct App
{
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}
    bool Start();

    VoltMod::Runtime& Runtime;
    ConfigManager Config;
    VoltMod::Database Db{Runtime.Scheduler};
    AdminManager Admins{Db, Config};
};
```

Nothing survives `OnUnload`, so `volt reload` starts from clean state. Because
`App` is destroyed before `Runtime`, its subscriptions are removed while their
services are still alive.

## Load order

1. The base seeds this module's hook dispatch pointer from the host, creates the
   runtime, and starts each framework subsystem as a step in `runtime.LoadSteps`.
2. It subscribes to the host's engine events, then calls
   `OnRegisterHooks(runtime, hooks)`.
3. It calls `OnLoad(runtime)`. Returning `false` runs `OnUnload`, removes hooks,
   and destroys the runtime. The base hands the host the first required step that
   failed, or `OnLoad returned false`, and the host logs it with the refusal.

The standard prelude (settings as a required step, then translations) is one call:

```cpp
bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "my-plugin"}))
        return false;
    InstallPolicy();
    return true;
}
```

### Installing the policy

`runtime.Policy` is how the framework asks your plugin who may do what. Fill in
the members you enforce before registering permission-gated commands - anything
that declares a permission is denied while `HasPermission` is unset, and the load
report says so:

```cpp
void App::InstallPolicy()
{
    auto& policy = Runtime.Policy;
    policy.HasPermission = [this](int64_t steamId, std::string_view perm) {
        return Access.HasAnyPermission(steamId, std::string(perm));
    };
    // Immunity only. The console has no caller and self-targeting is allowed, both
    // decided by Policy::Authorize before this is consulted.
    policy.CanTarget = [this](const VoltMod::Player& caller, const VoltMod::Player& target) {
        return Access.CanTarget(caller.SteamId(), target.SteamId());
    };
    policy.Reply = [this](int slot, std::string_view message) { Chat.Reply(slot, message); };
    policy.Broadcast = [this](const VoltMod::Authorized& who, std::string_view key) {
        if (who.Target)
            Chat.BroadcastAction(std::string(key), who.Caller.Name(), who.Target->Name());
    };
}
```

Commands with no permission stay available without a policy. Call
`Runtime.Policy.Authorize(callerRef, targetRef, permission)` wherever plugin code
needs the same decision; do not reimplement its steps. The full outcome table
is in @ref players_guide "Players".

`LoadStandardConfig` uses your config type's `LoadSettings` when it has one
(the load-then-validate convention), otherwise `JsonConfig::Load`. It applies
`plugin.locale` when the settings struct embeds
@ref VoltMod::StandardPluginSettings. Use `{.Translations = false}` for
a plugin that ships no translations.

## Other plugins in the same process

The host loads every installed plugin into the server process and hands each
engine event to them one after another, in load order. Load order follows the
dependencies declared in `voltmod_add_plugin` (`DEPENDS` and
`OPTIONAL_DEPENDS`), with ties broken alphabetically; unload is the reverse. So a
plugin that declares `DEPENDS admin-system` sees every event after admin-system
has seen it, and is unloaded before it.

A console command is offered to the plugins in that same order until one
consumes it. Consuming stops there: later plugins are not offered the command,
and the engine's own handling of it is blocked once. This is what `OnPlayerChat`
returning `true` does for a `say` or `say_team` line, and what casting a ballot
does for `vote`. Two plugins therefore cannot both answer one `!ban`.

## Registering commands

Commands are registered from your `App::Start()`, by code that already holds what the
handlers need:

```cpp
// src/Commands/BanCommands.cpp
namespace Args = VoltMod::Args;

void RegisterBanCommands(VoltMod::CommandManager& commands, App& app)
{
    commands.Add("ban").Permission("d").Run(
        [&app](VoltMod::Caller c, Args::Target t, Args::Duration d)
            -> VoltMod::Result<VoltMod::Reply> { return app.Punishments.Ban(c, *t.Value, d.Value); });
}

// App.cpp
void App::RegisterCommands()
{
    Commands::RegisterBanCommands(Runtime.Commands, *this);
}
```

`CommandManager` owns registrations until unload. Event, timer, and hook
subscriptions still belong in `_subs`, declared after the state they capture.

## Load steps: LoadSteps

`runtime.LoadSteps` runs named load steps and remembers only the ones that fail. `Runtime::Start`
already runs the framework subsystems through it. A step returns `Status`:

- `Optional(name, step)`: when it fails, the load continues without that feature.
- `Required(name, step)`: when it fails, return `false` from `OnLoad`. The base passes the reason
  back to the host, which logs it as the reason the plugin was refused.

Both return whether the step succeeded, so a later step can depend on an earlier one:

```cpp
auto& steps = Runtime.LoadSteps;

if (!steps.Required("Configuration", [this] { return Config.Load("addons/my-plugin/configs/settings.jsonc"); }))
    return false;  // the host logs: "Configuration: <reason>"

const bool database = steps.Optional("Database", [this] { return ConnectDatabase(); });
if (database)
    steps.Optional("Admins", [this] { return LoadAdminData(); });  // no second error while the database is down
```

After `OnLoad`, the base logs the step count and load time, plus one line per failed step. Code
that cannot fail does not need to be a step.

## Status sections: StatusService

`runtime.Status` combines named diagnostic sections. The framework supplies
build, load, and uptime sections. Plugins may add their own and expose
the report through a console command:

```cpp
Runtime.Status.RegisterSection("db", [this] {
    return VoltMod::Json::Write(DbSection{.connected = Db.IsConnected()});
});

Runtime.Status.InstallCommand("my_status", "Report plugin health; 'my_status json' emits STATUS_JSON.",
                              [this] { return Db.IsConnected(); });
```

`my_status` prints a human-readable report. `my_status json` emits one
`STATUS_JSON {...}` line for RCON tooling. The host reads the same sections:
`volt status <name>` prints this plugin's JSON and `volt status` prints every
loaded plugin's. The top-level `healthy` value is the
predicate's answer, or `true` without one. The command unregisters on unload.

Sections capture `this`, so keep them on an object the `Runtime` outlives. The `App` is
destroyed first, and a section left holding a dangling pointer is a lifetime bug even if
nothing calls it in the gap.

Keep JSON sections compact (counts and names, not full lists), because RCON's console capture can truncate large responses.

## Overrides

| Override | Fires | Notes |
|----------|-------|-------|
| `Info()` | At load, and for `volt list` and `volt status` | Required |
| `OnLoad(runtime)` | Once the runtime is live | Required; `false` rejects the load |
| `OnUnload()` | On unload, before the runtime is destroyed | Drop whatever `OnLoad` built |
| `OnServerStartup(mapName)` | Each map start, after event listeners are attached | The engine has just reset convars and run the game-mode cfgs |
| `OnPlayerChat(Player*, string_view, bool team)` | On `say`/`say_team` | Default dispatches registered chat commands and swallows handled ones; override to customize (an override replaces the dispatch wholesale, as admin-style chat services do) |
| `OnRegisterHooks(runtime, hooks)` | Once during load | Custom engine hooks |

The connection lifecycle is **not** an override. Subscribe to
`runtime.Players.Connected`, `.FullyConnected`, `.SettingsChanged` and
`.Disconnected` from `OnLoad` and keep the `Subscription`s on the object that
owns the state - see @ref players_guide "Players".

A hook body reaches the runtime through the state `OnLoad` built, not through the
base: the host calls these without a runtime argument, so keep whatever `OnLoad`
handed you on the object that needs it.

## Cleanup on unload

Cleanup belongs in a member destructor or a @ref VoltMod::Subscription
held beside the state its handler captures:

```cpp
class Bhop
{
    Bhop(VoltMod::Runtime& runtime) : _rt(runtime)
    {
        _spawn = _rt.GameEvents.On<VoltMod::PlayerSpawn>([this](const VoltMod::PlayerSpawn& e) { OnSpawn(e.Slot); });
        _slots = _rt.Slots.Changed += [this](int slot) { _state.Reset(slot); };
    }
    VoltMod::Subscription _spawn;   // removed before the members above it are destroyed
    VoltMod::Subscription _slots;
};
```

Events, game events, scheduler timers, and scoped hooks return a `[[nodiscard]]`
`Subscription`. Store it after the state captured by its handler so it is
destroyed first. Dropping a scheduler subscription cancels the timer. Other
shutdown work, such as draining the database or withdrawing a published
interface, belongs in the `App` destructor.

## Typed game events

Subscribe to game events as structs instead of string + `GetInt` pairs. The structs live in `VoltMod` (`VoltMod/Events/EventTypes.hpp`); @ref sdk_events_guide lists them.

```cpp
using VoltMod::PlayerDeath;

_playerDeath = Runtime.GameEvents.On<PlayerDeath>([this](const PlayerDeath& e) {
    if (e.VictimSlot >= 0)
        Effects.CancelAllForSlot(e.VictimSlot);
});
```

There is no string form: consuming an unmodeled event means adding its struct to `EventTypes.hpp` first; see @ref sdk_events_guide.

## Custom hooks

Nothing has to be declared at namespace scope. `VoltMod::HookInterface` reads the vtable slot
from the member function pointer and installs the hook, handing back the `Subscription` that
removes it.

A handler is any callable taking the hooked object as its first parameter. A before-handler returns
`VoltMod::HookResult<Ret>`, or nothing at all when it only observes. Pass `nullptr` for the handler
you do not want; here only the before-handler runs.

For per-tick player movement you don't need a custom hook at all: the framework ships @ref VoltMod::Movement (see @ref sdk_hooks_guide).

```cpp
#include <VoltMod/Unsafe/Hook.hpp>

void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime, VoltMod::Subscriptions& hooks)
{
    hooks.Add(VoltMod::HookInterface(&IVEngineServer2::SetClientListening, runtime.Unsafe.Interfaces.Engine,
                                     [this](IVEngineServer2& engine, CPlayerSlot receiver, CPlayerSlot sender,
                                            bool listen) -> VoltMod::HookResult<bool> {
                                         if (!Muted(receiver, sender))
                                             return {};
                                         return VoltMod::HookResult<bool>::Block(false);
                                     }));
}
```

A default-constructed `HookResult`, which is what `return {}` gives you, leaves the engine's own
result in place. `Replace` substitutes the return value but still calls the original, and `Block`
substitutes it and skips the original. `VoltMod::CallOriginal` runs the engine's own
implementation from inside a handler when you need its side effects as well.

Add the subscription to `hooks`, do not keep it in a member of your plugin class. The base
removes custom hooks before `OnUnload` runs, so a hook body cannot fire into state `OnUnload`
has already released - including after an `OnLoad` that returned false. A subscription held in a
derived member would instead outlive the whole plugin graph, because the derived object is the
`VOLTMOD_PLUGIN` global and its members live until the host frees the library.

## Configuration

Settings loading is one call through @ref VoltMod::JsonConfig; see @ref config_guide.
