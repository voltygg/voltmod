# Writing a plugin {#plugin_guide}

[TOC]

## The smallest plugin

A plugin is a library with one exported entry point, a `plugin.json` beside its `CMakeLists.txt`,
and an `App` class that owns everything for one load cycle.

```cpp
// src/App.hpp
#pragma once

#include <VoltMod/Api.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>

namespace MyPlugin
{

struct App final : VoltMod::Plugin
{
    explicit App(VoltMod::Runtime& runtime) : Plugin(runtime) {}

    /** Load config and register commands. Returning false aborts the load. */
    bool Load() override;

    ConfigManager Config;

private:
    /** Declared last, so handlers stop before the state they capture goes away. */
    VoltMod::Subscriptions _subs;
};

}  // namespace MyPlugin
```

```cpp
// src/App.cpp
#include "App.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginEntry.hpp>

VOLTMOD_PLUGIN(MyPlugin::App);

namespace MyPlugin
{

void RegisterCommands(VoltMod::CommandManager& commands);   // defined in src/Commands.cpp

bool App::Load()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config))
        return false;

    RegisterCommands(Runtime.Commands);
    return true;
}

}  // namespace MyPlugin
```

`VOLTMOD_PLUGIN` goes at global scope in exactly one `.cpp`, and
`<VoltMod/App/PluginEntry.hpp>` is included only there: it pulls in `BuildInfo.hpp`, which changes
every commit. The macro defines the plugin object, this module's hook dispatch pointer and
`VoltMod_PluginEntry`, the one symbol the host resolves.

Your `App` derives from @ref VoltMod::Plugin. The framework constructs it from `Runtime&` after
the runtime starts and destroys it before the runtime shuts down.

| Override | Required | Called |
| --- | --- | --- |
| `bool Load()` | yes | Once on load; `false` aborts the load |
| `void OnServerStartup(std::string_view mapName)` | no | At each map start |
| `bool OnPlayerChat(Player*, std::string_view message, bool teamChat)` | no | On `say` / `say_team`, in place of the default, which consumes menu input and then dispatches `!` and `.` commands. `true` swallows the line |

Keep custom engine hooks, signals and timers in the App's own `Subscriptions`.

Members initialize in declaration order, so an initializer may reference only members above it.
The `App` is destroyed before the `Runtime`, which is what lets its subscriptions unregister while
the services they point at are still alive.

### What the scaffold creates

`voltmod new-plugin <name>` writes `plugins/<name>/`:

| File | Holds |
| --- | --- |
| `CMakeLists.txt` | one `voltmod_add_plugin(<name>)` call |
| `plugin.json` | the manifest below |
| `src/App.hpp`, `src/App.cpp` | the load-cycle object graph and `VOLTMOD_PLUGIN` |
| `src/Commands.cpp` | the `!ping` command |
| `src/Config.hpp` | the settings struct and `ConfigManager` |
| `configs/settings.jsonc`, `configs/settings.schema.json` | operator settings and their schema |
| `configs/translations/en.json` | player-facing text |

Add `.cpp` files anywhere under `src/`; `voltmod_add_plugin` globs them.

## plugin.json

The manifest is hand-written and lives beside `CMakeLists.txt`. CMake reads `name` and `version`
from it at configure time; the host reads the copy installed at
`addons/voltmod/plugins/<name>/plugin.json`. An
unknown key is an error and the plugin is refused.

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "logTag": "MYPLUGIN",
  "description": "",
  "author": "",
  "dependencies": [],
  "optionalDependencies": []
}
```

| Key | Type | Default | Meaning |
| --- | --- | --- | --- |
| `name` | string | required | The plugin's directory under `addons/voltmod/plugins/` and its CMake target. All three must match. |
| `version` | string | required | Its version. CMake stamps it into the build info as `<version>+<short-sha>[-dirty]`, which is what `volt list` shows. |
| `logTag` | string | `name` | The prefix the host puts in front of every log line from this plugin. |
| `description` | string | `""` | One line, printed after the version by `volt list`. |
| `author` | string | `""` | Credit. The host does not print it. |
| `dependencies` | string[] | `[]` | Plugins that must be installed and loaded first. |
| `optionalDependencies` | string[] | `[]` | Plugins to load after when they are installed, ignored when they are not. |

Both dependency lists decide load order and what a reload takes down with it; see @ref host_guide.

The connection lifecycle is not an override. Subscribe to `Runtime.Players.Connected`,
`.FullyConnected`, `.SettingsChanged` and `.Disconnected` in `Load`; see @ref players_guide.
Custom hooks are in @ref sdk_hooks_guide, typed game events in @ref sdk_events_guide.

## Load steps

`runtime.LoadSteps` runs named steps and remembers the ones that fail. `Runtime::Start` already
runs the framework's subsystems through it. A step returns @ref VoltMod::Status.

```cpp
auto& steps = Runtime.LoadSteps;

const bool database = steps.Optional("Database", [this] { return ConnectDatabase(); });
if (database)
    steps.Optional("Admins", [this] { return LoadAdminData(); });  // no second error while it is down

if (!steps.Required("Migrations", [this] { return Migrate(); }))
    return false;
```

`Optional` continues without that feature. `Required` is for work the plugin cannot run without:
return `false` from `Load` and the base hands the first required failure to the host as
`<step>: <reason>`, which the host logs as the refusal. Both return whether the step succeeded.

After the load the base logs `N load steps in X ms` plus one line per failure. Work that cannot
fail does not need to be a step.

The standard prelude - settings as a required step, then translations - is one call:

```cpp
if (!VoltMod::LoadStandardConfig(Runtime, Config))
    return false;
```

It reads `addons/voltmod/plugins/<plugin>/configs/settings.jsonc` and then `configs/translations`. Pass
`{.SettingsFile = "configs/other.jsonc"}` or `{.Translations = false}` to change either. See
@ref config_guide.

## Status sections

`runtime.Status` combines named diagnostic sections into one report. The framework registers
`build`, `load` and `uptime`. Add your own in `Load` and expose the report as a console command:

```cpp
Runtime.Status.RegisterSection("db", [this] {
    return VoltMod::Json::Write(DbSection{.connected = Db.IsConnected()});
});

Runtime.Status.InstallCommand("my_status", "Report plugin health; 'my_status json' emits STATUS_JSON.",
                              [this] { return Db.IsConnected(); });
```

`my_status` prints text, `my_status json` emits one `STATUS_JSON {...}` line for RCON tooling, and
`volt status <name>` prints the same JSON from the host. The top-level `healthy` value is the
predicate's answer, or `true` when there is no predicate. The command unregisters on unload.

Keep sections compact - counts and names, not full lists - because RCON console capture truncates
long responses. A section capturing `this` must live on an object the `Runtime` outlives.

## Logging

`Log::Info`, `Log::Warn` and `Log::Error` from `<VoltMod/Core/Log.hpp>` format with `std::format`
and hand the line to the host, which prefixes the plugin's `logTag`. The host also sets the
minimum level, so a line below it is never formatted.

@ref VoltMod::Logger adds the name of the class that logged. It is in
`<VoltMod/App/Logger.hpp>`, not in `<VoltMod/Api.hpp>`:

```cpp
class BhopManager
{
    void OnTick(int elapsed)
    {
        _log.Info("ready in {}ms", elapsed);  // [MYPLUGIN] [BhopManager] ready in 12ms
    }

    VoltMod::Logger<BhopManager> _log;
};
```

Namespaces and template arguments are dropped, so `Reports::ReportQueue` logs as `[ReportQueue]`.
It holds nothing and takes no constructor argument.

`volt log <name> <info|warn|error>` raises or lowers one plugin's level while the server runs.

## Cleanup on unload

Nothing survives the App's destructor, so a `volt reload` starts from clean state. Cleanup belongs
in a member destructor or in a @ref VoltMod::Subscription held beside the state its handler captures:

```cpp
class Bhop
{
    Bhop(VoltMod::Runtime& runtime) : _rt(runtime)
    {
        _spawn = _rt.GameEvents.On<VoltMod::PlayerSpawn>([this](const VoltMod::PlayerSpawn& e) { OnSpawn(e.Slot); });
        _slots = _rt.Slots.Changed += [this](int slot) { _state.Reset(slot); };
    }

    VoltMod::Subscription _spawn;   // destroyed before the members above it
    VoltMod::Subscription _slots;
};
```

Events, game events, scheduler timers and scoped hooks return a `[[nodiscard]]` `Subscription`
that unregisters on destruction; dropping a scheduler subscription cancels the timer. Draining a
database or withdrawing a published interface belongs in the `App` destructor. Commands are owned
by `CommandManager` for the load cycle and need no cleanup.

Whatever the plugin still holds when its view closes, the host takes back and logs:

```text
my-plugin left a frame subscription behind; the host dropped it.
my-plugin left the service 'bans.IBanService/1' published; the host withdrew it.
```

Each of those is a bug in the plugin: something outlived the `App` that built it. Command names
are different: the host removes them with the plugin and says nothing.

## Install layout

`voltmod_add_plugin` defines an install component named after the plugin. Staged into a server's
`game/csgo`, the tree is:

```text
addons/
  metamod/voltmod.vdf                     the host's Metamod plugin file, the server's only one
  voltmod/
    bin/win64/voltmod.dll                 or bin/linuxsteamrt64/voltmod.so
    gamedata/gamedata.jsonc
    schema/server.json                    written by the server once a map has run
    plugins/my-plugin/
      plugin.json
      my-plugin.dll                       or my-plugin.so
      configs/
        settings.jsonc                    seeded once, never overwritten
        settings.schema.json
        translations/en.json
```

A plugin has no `.vdf` or `bin` directory of its own. `runtime.PluginFile("configs/x")` builds
`addons/voltmod/plugins/<name>/configs/x` for any file the plugin reads at run time.

`uv run poe build --install <name>` stages and merges both trees. By hand:

```sh
cmake --install build/<preset> --component host      --prefix dist
cmake --install build/<preset> --component my-plugin --prefix dist
```

then copy `dist/addons` into `game/csgo`, leaving operator-edited settings alone.
