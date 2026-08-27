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

`<VoltMod/Api.hpp>` covers everything above: `MetamodPlugin`, `Runtime`,
players, commands, and the core vocabulary. It deliberately stops there - no
menu building, no nlohmann, no raw interfaces - so a `.cpp` that needs one of
those adds the specific header instead:

| Need | Header |
|---|---|
| Menus (`MenuBuilder`, `Flow`, presets, options) | `<VoltMod/Menu/Api.hpp>` |
| More of Entities (`EntityRef`, `Field`, `Items`, `ConVar`) or Hooks (`Movement`, `Teleport`, game events) | `<VoltMod/Entities/Api.hpp>`, `<VoltMod/Hooks/Api.hpp>` |
| A JsonConfig-backed settings struct | `<VoltMod/App/Config.hpp>` (see @ref config_guide) |
| Raw interfaces, gamedata, or vtable hooking | `<VoltMod/Unsafe/Api.hpp>` |
| PostgreSQL | `<VoltMod/Database/Api.hpp>` (see @ref database_guide) |

See @ref getting_started "Getting started" for the full table.

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

Commands with no permission stay available without a policy. Ask the gate yourself
with `Runtime.Policy.Authorize(callerRef, targetRef, permission)` wherever your own
code needs the same answer - never re-implement its steps. The full outcome table
is in @ref players_guide "Players".

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
namespace Args = VoltMod::Args;

void RegisterBanCommands(VoltMod::CommandManager& commands, App& app,
                         std::vector<VoltMod::Subscription>& subs)
{
    subs.push_back(commands.Add("ban").Permission("d").Run(
        [&app](VoltMod::Caller c, Args::Target t, Args::Duration d)
            -> VoltMod::Result<VoltMod::Reply> { return app.Punishments.Ban(c, *t.Value, d.Value); }));
}

// App.cpp
void App::RegisterCommands()
{
    Commands::RegisterBanCommands(Runtime.Commands, *this, _subs);
}
```

`Run` returns the registration. Keep it in the same `_subs` vector the rest of
your listeners live in, declared last so the handlers stop before the state they
captured goes away.

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
    return nlohmann::json{{"connected", Db.IsConnected()}}.dump();
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
| `OnServerStartup(mapName)` | Each map start, after event listeners are attached | The engine has just reset convars and run the game-mode cfgs |
| `OnPlayerChat(Player*, string_view, bool team)` | On `say`/`say_team` | Default dispatches registered chat commands and swallows handled ones; override to customize (an override replaces the dispatch wholesale, as admin-style chat services do) |
| `OnRegisterHooks(runtime)` | Once during load | Custom SourceHook hooks |

The connection lifecycle is **not** an override. Subscribe to
`runtime.Players.Connected`, `.FullyConnected`, `.SettingsChanged` and
`.Disconnected` from `OnLoad` and keep the `Subscription`s on the object that
owns the state - see @ref players_guide "Players".

`Rt()` is available inside the base for hook bodies, which SourceHook calls without a
runtime argument. Everywhere else, use what `OnLoad` handed you.

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

Every registration in the framework returns a `[[nodiscard]]` `Subscription`: `+=` on an
@ref VoltMod::Event member, `On<T>` for game events, `Scheduler::Delay`/`NextTick`/`Repeat`/
`EveryFrame`, and `VOLTMOD_SCOPED_HOOK`. Store each one beside the state its handler captured,
in a member declared after that state so it unregisters first. `Subscription` is move-only, so a
registration you drop on the floor is a compiler warning rather than a handler that fires into
freed memory - and for the scheduler, dropping it *cancels* the timer, which is what keeps a
pending one-shot from outliving what it was going to touch. Work that is not a registration, such
as draining a database or withdrawing a published interface, belongs in your `App`'s destructor.

## Typed game events

Subscribe to game events as structs instead of string + `GetInt` pairs. The structs live in `VoltMod` (`VoltMod/Events/EventTypes.hpp`): `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerBlind`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `BulletImpact`, `RoundStart`, `RoundEnd`, `RoundPrestart`, `VoteCast`.

```cpp
using VoltMod::PlayerDeath;

_playerDeath = Runtime.GameEvents.On<PlayerDeath>([this](const PlayerDeath& e) {
    if (e.VictimSlot >= 0)
        Effects.CancelAllForSlot(e.VictimSlot);
});
```

There is no string form: consuming an unmodeled event means adding its struct to `EventTypes.hpp` first; see @ref sdk_events_guide.

## Custom hooks

`SH_DECL_HOOKn` must still appear once at namespace scope in your .cpp (it expands to hook-manager classes; no helper can wrap it). The add/remove pairing *is* automated: `VOLTMOD_SCOPED_HOOK` installs the hook and yields a `Subscription` that removes it.

For per-tick player movement you don't need a custom hook at all: the framework ships @ref VoltMod::Movement (see @ref sdk_hooks_guide).

```cpp
#include <VoltMod/Unsafe/HookMacros.hpp>

SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);

void MyPlugin::OnRegisterHooks(VoltMod::Runtime& runtime)
{
    _listening = VOLTMOD_SCOPED_HOOK(IVEngineServer2, SetClientListening, runtime.Unsafe.Interfaces.Engine,
                                    SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
}
```

## Configuration

Settings loading is one call through @ref VoltMod::JsonConfig; see @ref config_guide.
