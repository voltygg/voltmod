# Plugin Base {#plugin_guide}

[TOC]

@ref CS2Kit::Core::PluginBase owns everything a Metamod plugin re-types by hand: the ISmmPlugin metadata getters, the Load/Unload skeleton, the standard SourceHook hooks, the `PlayerManager` lifecycle, and the construction of your own manager container. You subclass it with your `Managers` struct, return your metadata, and override only the callbacks you care about.

`MetamodPluginBase` (the non-template parent) is the same thing without the manager container - use it only if you manage your own `App()` equivalent.

## The skeleton

```cpp
#include <CS2Kit/Api.hpp>

class MyPlugin : public CS2Kit::PluginBase<MyNs::Managers>
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return { .Name = "My Plugin", .Author = "me", .Version = "1.0.0", .LogTag = "MINE" };
    }

    bool OnLoad(bool late) override;
};

// In the .cpp:
MyPlugin g_MyPlugin;
PLUGIN_EXPOSE(MyPlugin, g_MyPlugin);   // + PLUGIN_GLOBALVARS() in the header

namespace MyNs { Managers& App() { return MyPlugin::App(); } }
```

`PLUGIN_EXPOSE` / `PLUGIN_GLOBALVARS` stay in your code - they define the per-plugin SourceHook globals the base links against.

## What Load does, in order

1. `PLUGIN_SAVEVARS()` + `CS2Kit::Initialize()` - interface resolution, gamedata, every kit subsystem.
2. `OnRegisterHooks()` - your custom SourceHook hooks.
3. `OnCreateInstances()` - `PluginBase` constructs your `Managers` here, after the kit services are live, so manager member initializers may call `Engine()`.
4. Your `OnLoad(late)`. Returning `false` rejects the load - the Defer stack runs and `CS2Kit::Shutdown()` is called first, so a failed init never leaks.

A typical `OnLoad` reads config, installs the policy, and ingests registered commands:

```cpp
bool MyPlugin::OnLoad(bool late)
{
    if (!App().Config.Load("addons/my-plugin/configs/settings.jsonc"))
        return false;

    Engine().Translations.SetLanguage(App().Config.Get().plugin.locale);
    Engine().Translations.Load("addons/my-plugin/configs/translations");

    Engine().Policy = {
        .HasPermission = ...,   // see the architecture guide
        .Reply = [](int slot, std::string_view msg) { Engine().Messages.Reply(slot, msg); },
    };

    Engine().Commands.RegisterAll(CS2Kit::Registry<CS2Kit::CommandSpec>::Items());
    return true;
}
```

## Load stages: LoadReport

`Engine().LoadReport` records named, timed load stages. `CS2Kit::Initialize` already runs every kit subsystem through it; run your own `OnLoad` steps through `Run()` too and the base class does the rest - it logs an aligned per-stage summary after load and, when `OnLoad` fails, copies `FirstFailure()` into Metamod's error buffer so `meta list` shows the actual reason instead of a generic message.

```cpp
auto& report = Engine().LoadReport;

const auto config = report.Run("Configuration", [] {
    if (!App().Config.Load("addons/my-plugin/configs/settings.jsonc"))
        return CS2Kit::StageResult::Failed("failed to load settings.jsonc");
    return CS2Kit::StageResult::Ok();
});
if (config == CS2Kit::StageStatus::Failed)
    return false;                        // base surfaces "Configuration: failed to load settings.jsonc"

report.Run("Database", [] {
    if (!App().Db.Start(App().Config.Get().database))
        return CS2Kit::StageResult::Degraded("unavailable");  // load continues, reduced functionality
    return CS2Kit::StageResult::Ok();
});

report.Run("Admins", [&] {
    if (!report.IsOk("Database"))        // dependency-aware skip: no confusing secondary error
        return CS2Kit::StageResult::Skipped("database unavailable");
    return LoadAdminData();
});
```

Statuses: `Ok`, `Degraded` (loaded with reduced functionality), `Skipped` (dependency not Ok), `Failed` (aborts the load when you return `false`). `IsOk()` is true only for `Ok` - a degraded dependency skips its dependents.

## Status sections: StatusService

`Engine().Status` aggregates named sections into one diagnostics report. The kit registers `build` (PluginInfo), `load` (LoadReport rollup), `gamedata` (resolution results), and `uptime`; plugins register their own in `OnLoad` and call `InstallCommand` to expose the report:

```cpp
Engine().Status.RegisterSection("db", [] {
    return nlohmann::json{{"connected", Engine().LoadReport.IsOk("Database")}};
});

Engine().Status.InstallCommand("my_status", "Report plugin health; 'my_status json' emits STATUS_JSON.",
                               [] { return Engine().LoadReport.IsOk("Database"); });
```

`my_status` prints the sections for humans; `my_status json` emits them as one `STATUS_JSON {...}` line RCON scripts can find amid console noise. Both carry a top-level `healthy` flag: no load stage `Failed`, ANDed with the optional predicate you pass (omit it for the baseline alone). The command unregisters itself on unload.

Keep JSON sections compact (counts and names, not full lists) - RCON's console capture can truncate large responses.

## Overrides

| Override | Fires | Notes |
|----------|-------|-------|
| `Info()` | Metadata queries | Required |
| `OnLoad(late)` | After services + managers exist | Required; `false` rejects the load |
| `OnUnload()` | On unload, before the Defer stack | Prefer `Defer()` for most cleanup |
| `OnPlayerConnect(Player*)` | After `PlayerManager` adds the player | Non-null in the normal flow |
| `OnPlayerFullyConnected(Player*)` | Post `ClientFullyConnect`, once the player is in the server | First point their name and convars are meaningful; may be null - guard it |
| `OnPlayerSettingsChanged(Player*)` | The client changed a replicated setting (name, userinfo cvars) | Fires on every change, including the burst the engine sends at connect - debounce if you act on it; may be null |
| `OnPlayerDisconnect(Player*)` | Before the player is removed | May be null - guard it |
| `OnPlayerChat(Player*, string_view, bool team)` | On `say`/`say_team` | Return `true` to swallow the message; dispatch commands here via `Engine().Commands.HandleChatMessage` |
| `OnRegisterHooks()` | Once during load | Custom SourceHook hooks |

## Teardown: Defer

`Defer(fn)` registers cleanup that runs in reverse order (LIFO) on unload and on a failed load. Register teardown right next to its setup:

```cpp
if (db.Start(App().Config.Get().database))
    Defer([] { App().Db.Stop(); });    // drains queued writes before managers die
```

## Typed game events

Listen for game events as structs instead of string + `GetInt` pairs. The structs live in `CS2Kit::Events` (`Sdk/GameEvents.hpp`): `PlayerDeath`, `PlayerSpawn`, `PlayerJump`, `PlayerHurt`, `PlayerTeam`, `PlayerConnectFull`, `WeaponFire`, `RoundStart`, `RoundEnd`, `RoundPrestart`.

```cpp
namespace Events = CS2Kit::Events;

Engine().Events.Listen<Events::PlayerDeath>([](const Events::PlayerDeath& e) {
    if (e.VictimSlot >= 0)
        App().Effects.CancelAllForSlot(e.VictimSlot);
});
```

The stringly `Listen("event_name", ...)` overload stays as the escape hatch for unmodeled events - see @ref sdk_events_guide.

## Custom hooks

`SH_DECL_HOOKn` must still appear once at namespace scope in your .cpp (it expands to hook-manager classes; no helper can wrap it). The add/remove pairing *is* automated: `CS2KIT_SCOPED_HOOK` installs the hook and queues the matching removal on the Defer stack in one statement.

For per-tick player movement you don't need a custom hook at all - the kit ships @ref CS2Kit::Sdk::MovementHook (see @ref sdk_hooks_guide).

```cpp
#include <CS2Kit/Core/HookMacros.hpp>

SH_DECL_HOOK3(IVEngineServer2, SetClientListening, SH_NOATTRIB, 0, bool, CPlayerSlot, CPlayerSlot, bool);

void MyPlugin::OnRegisterHooks()
{
    CS2KIT_SCOPED_HOOK(IVEngineServer2, SetClientListening, Engine().Interfaces.Engine,
                       SH_MEMBER(this, &MyPlugin::Hook_SetClientListening), false);
}
```

## Configuration

Settings loading is one call through @ref CS2Kit::Core::JsonConfig - see @ref config_guide.
