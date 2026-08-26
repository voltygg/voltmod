# Plugin base {#plugin_guide}

[TOC]

@ref VoltMod::MetamodPlugin owns the repeated Metamod integration:
ISmmPlugin metadata getters, the Load/Unload flow, the
standard SourceHook hooks, and the `PlayerManager` lifecycle. It creates one
@ref VoltMod::Runtime for each load cycle and passes it to `OnLoad`. You provide
metadata, build your object graph, and override only the callbacks you need.

## The skeleton

```cpp
#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

class MyPlugin final : public VoltMod::MetamodPlugin
{
protected:
    VoltMod::PluginInfo Info() const override
    {
        // WithBuildInfo stamps Version/Date/Commit from <VoltMod/BuildInfo.hpp>.
        return VoltMod::WithBuildInfo({ .Name = "My Plugin", .Author = "me", .LogTag = "MINE" });
    }

    bool OnLoad(VoltMod::Runtime& runtime, bool late) override
    {
        _app.emplace(runtime);
        return _app->Start();
    }

    void OnUnload() override { _app.reset(); }

private:
    std::optional<MyNs::App> _app;
};

// In the .cpp: the global instance and PLUGIN_EXPOSE in one line:
VOLTMOD_PLUGIN(MyPlugin);
```

`VOLTMOD_PLUGIN` expands the per-plugin SourceHook globals (`PLUGIN_EXPOSE`)
used by the base. The matching extern declarations are in
`MetamodPlugin.hpp`, so the plugin header needs no additional declarations.

Your `App` is a plain struct holding whatever the plugin owns for one load cycle. It takes the runtime by reference and passes on what each member needs; declaration order is construction order, so a member initializer may only reference members declared **above** it.

```cpp
struct App
{
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}
    bool Start();

    VoltMod::Runtime& Runtime;
    ConfigManager Config;
    VoltMod::PostgresDatabase Db{Runtime.Scheduler};
    AdminManager Admins{Db, Config};
};
```

Nothing survives `OnUnload`, so a `meta reload` starts from clean state. Because the `App` is destroyed before the `Runtime`, every subscription it holds is removed while the service it points at is still alive.

## Load order

1. `PLUGIN_SAVEVARS()`, then the `Runtime` is constructed and `Runtime::Start` runs: interface resolution, gamedata, and every framework subsystem, each as a `LoadReport` stage.
2. The standard SourceHook hooks, then `OnRegisterHooks(runtime)` for yours.
3. Your `OnLoad(runtime, late)`. Returning `false` rejects the load; `OnUnload()` runs and the `Runtime` is destroyed, so a failed init never leaks. A bare `return false` with no Failed stage recorded gets a synthetic "OnLoad" failure stage, so `meta list` always names a reason.

The standard prelude (config plus translations, recorded as LoadReport stages) is one call:

```cpp
bool App::Start()
{
    if (!VoltMod::LoadStandardConfig(Runtime, Config, {.Addon = "my-plugin"}))
        return false;
    // Install Runtime.Policy before registering permission-gated commands.
    // Commands with no permission remain available without a policy.
    return true;
}
```

`LoadStandardConfig` uses your config type's `LoadSettings` when it has one
(the load-then-validate convention), otherwise `JsonConfig::Load`. It applies
`plugin.locale` when the settings struct embeds
@ref VoltMod::StandardPluginSettings. Use `{.Translations = false}` for
a plugin that ships no translations.

## Registering commands

Commands are registered from your `App::Start()`, by code that already holds what the
handlers need:

```cpp
// src/Commands/BanCommands.cpp
void RegisterBanCommands(VoltMod::CommandManager& commands, App& app)
{
    commands.Register({
        .Name = "ban",
        .Permission = "d",
        .Handler = [&app](const VoltMod::CommandContext& ctx) { return app.Punishments.Ban(ctx); },
    });
}

// App.cpp
void App::RegisterCommands()
{
    Commands::RegisterBanCommands(Runtime.Commands, *this);
}
```

## Load stages: LoadReport

`runtime.LoadReport` records named, timed stages. `Runtime::Start` already runs
the framework subsystems through it. Run plugin initialization through `Run()` as
well; the base logs an aligned summary and copies `FirstFailure()` into
Metamod's error buffer when `OnLoad` fails, so `meta list` shows the actual
reason.

```cpp
auto& report = Runtime.LoadReport;

const auto config = report.Run("Configuration", [this] {
    if (!Config.Load("addons/my-plugin/configs/settings.jsonc"))
        return VoltMod::StageResult::Failed("failed to load settings.jsonc");
    return VoltMod::StageResult::Ok();
});
if (config == VoltMod::StageStatus::Failed)
    return false;                        // base surfaces "Configuration: failed to load settings.jsonc"

report.Run("Database", [this] {
    if (!Db.Start(Config.Get().database))
        return VoltMod::StageResult::Degraded("unavailable");  // load continues, reduced functionality
    return VoltMod::StageResult::Ok();
});

report.Run("Admins", [&] {
    if (!report.IsOk("Database"))        // dependency-aware skip: no confusing secondary error
        return VoltMod::StageResult::Skipped("database unavailable");
    return LoadAdminData();
});
```

Statuses: `Ok`, `Degraded` (loaded with reduced functionality), `Skipped` (dependency not Ok), `Failed` (aborts the load when you return `false`). `IsOk()` is true only for `Ok`, so a degraded dependency skips its dependents.

## Status sections: StatusService

`runtime.Status` aggregates named sections into one diagnostics report. The
framework registers `build` (PluginInfo), `load` (LoadReport rollup), `gamedata`
(resolution results), and `uptime`. Plugins can register sections in `OnLoad`
and call `InstallCommand` to expose the report:

```cpp
Runtime.Status.RegisterSection("db", [this] {
    return nlohmann::json{{"connected", Db.IsConnected()}};
});

Runtime.Status.InstallCommand("my_status", "Report plugin health; 'my_status json' emits STATUS_JSON.",
                              [this] { return Db.IsConnected(); });
```

`my_status` prints the sections for humans; `my_status json` emits them as one `STATUS_JSON {...}` line RCON scripts can find amid console noise. Both carry a top-level `healthy` flag: no load stage `Failed`, ANDed with the optional predicate you pass (omit it for the baseline alone). The command unregisters itself on unload.

Sections capture `this`, so keep them on an object the `Runtime` outlives. The `App` is
destroyed first, and a section left holding a dangling pointer is a lifetime bug even if
nothing calls it in the gap.

Keep JSON sections compact (counts and names, not full lists), because RCON's console capture can truncate large responses.

## Overrides

| Override | Fires | Notes |
|----------|-------|-------|
| `Info()` | Metadata queries | Required |
| `OnLoad(runtime, late)` | Once the runtime is live | Required; `false` rejects the load |
| `OnUnload()` | On unload, before the runtime is destroyed | Drop whatever `OnLoad` built |
| `OnPlayerConnect(Player*)` | After `PlayerManager` adds the player | Non-null in the normal flow |
| `OnPlayerFullyConnected(Player*)` | Post `ClientFullyConnect`, once the player is in the server | First point their name and convars are meaningful; may be null, so guard it |
| `OnPlayerSettingsChanged(Player*)` | The client changed a replicated setting (name, userinfo cvars) | Fires on every change, including the burst the engine sends at connect, so debounce if you act on it; may be null |
| `OnPlayerDisconnect(Player*)` | Before the player is removed | May be null, so guard it |
| `OnPlayerChat(Player*, string_view, bool team)` | On `say`/`say_team` | Default dispatches registered chat commands and swallows handled ones; override to customize (an override replaces the dispatch wholesale, as admin-style chat services do) |
| `OnRegisterHooks(runtime)` | Once during load | Custom SourceHook hooks |

`Rt()` is available inside the base for hook bodies, which SourceHook calls without a
runtime argument. Everywhere else, use what `OnLoad` handed you.

## Cleanup on unload

Cleanup belongs in a member destructor or a @ref VoltMod::Subscription
held beside the state its callback captures:

```cpp
class Bhop
{
    Bhop(VoltMod::Runtime& runtime) : _rt(runtime)
    {
        _spawn = _rt.Events.Listen<Events::PlayerSpawn>([this](const auto& e) { OnSpawn(e.Slot); });
    }
    VoltMod::Subscription _spawn;   // removed before the members above it are destroyed
};
```

`Subscription` is move-only and `[[nodiscard]]`, so a registration you drop on the floor
is a compiler warning rather than a listener that fires into freed memory. Work that is
not a registration, such as draining a database or withdrawing a published interface, belongs
in your `App`'s destructor.

## Typed game events

Listen for game events as structs instead of string + `GetInt` pairs. The structs live in `VoltMod` (`VoltMod/Events/EventTypes.hpp`): `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `RoundStart`, `RoundEnd`, `RoundPrestart`.

```cpp
using VoltMod::PlayerDeath;

_playerDeath = Runtime.Events.Listen<PlayerDeath>([this](const PlayerDeath& e) {
    if (e.VictimSlot >= 0)
        Effects.CancelAllForSlot(e.VictimSlot);
});
```

The stringly `Listen("event_name", ...)` overload stays as the escape hatch for unmodeled events; see @ref sdk_events_guide.

## Custom hooks

`SH_DECL_HOOKn` must still appear once at namespace scope in your .cpp (it expands to hook-manager classes; no helper can wrap it). The add/remove pairing *is* automated: `VOLTMOD_SCOPED_HOOK` installs the hook and yields a `Subscription` that removes it.

For per-tick player movement you don't need a custom hook at all: the framework ships @ref VoltMod::Movement (see @ref sdk_hooks_guide).

```cpp
#include <VoltMod/Unsafe/HookMacros.hpp>

SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);

void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime)
{
    _listening = VOLTMOD_SCOPED_HOOK(IVEngineServer2, SetClientListening, runtime.Interfaces.Engine,
                                    SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
}
```

## Configuration

Settings loading is one call through @ref VoltMod::JsonConfig; see @ref config_guide.
