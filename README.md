# CS2Kit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://suxrobgm.github.io/cs2-kit/)

A C++23 toolkit for building Counter-Strike 2 server plugins on Metamod:Source 2.0.

You describe your plugin's behavior as data - commands, menu rows, effects, database rows - and CS2Kit owns the machinery around it: engine setup, hooks, player tracking, permission gating, message transport, and teardown.

> **Work in progress.** The API is still evolving and may change between versions.

## What you get

- **Plugin base** - subclass `PluginBase<Managers>` and the Metamod lifecycle, standard hooks, player tracking, and your own manager container are wired up; cleanup is a LIFO `Defer` stack.
- **Declarative commands** - a `CommandSpec` self-registers where it's defined; targets, durations, and SteamIDs are resolved and validated before your handler runs, with localized error replies.
- **Target selectors** - `@all`, `@me`, `@t`, `@ct`, `@dead`, `@random`, `#slot`, SteamIDs, and name fragments, with immunity applied through your injected policy.
- **Menus** - WASD-navigated in-game menus: typed rows, policy-aware context rows for admin/target actions, ready-made pickers, and the `Flow` wizard for multi-step "pick duration → pick reason → confirm" chains.
- **One policy hook** - permissions, immunity, replies, and broadcasts are injected once via `Engine().Policy`; the kit ships no admin model of its own.
- **Messages** - one service for chat, center print, center-HTML, and alerts, with per-player translations (`ReplyKey`) and chat colors.
- **Typed game events** - `Listen<PlayerDeath>(...)` instead of string names and `GetInt` calls; the raw overload stays for unmodeled events.
- **PostgreSQL** (optional) - async-first client (worker thread owns the connection, completions on the game thread), column-table row mapping that generates the INSERT/SELECT/parse code, and a migration runner. Gated behind `CS2KIT_ENABLE_POSTGRES`.
- **HTTP** - async requests with game-thread completions, plus config-driven JSON endpoint helpers.
- **Project + plugin scaffolding** - `scripts/init_project.py` stamps a complete buildable project around the vendored kit (root CMake, presets, conanfile, poe tasks); `scripts/new_plugin.py` adds more plugins from the `templates/plugin/` tree.
- **One-call plugin builds** - `cs2_add_plugin(<name>)` declares the whole Metamod module: sources, SDK glue, output layout, the generated `.vdf`, and per-plugin install components.

## Quick start

Start a plugin project from an empty directory:

```sh
mkdir my-cs2-plugins && cd my-cs2-plugins
git init
git submodule add https://github.com/voltygg/cs2-kit.git vendor/cs2-kit
git submodule update --init --recursive
python vendor/cs2-kit/scripts/init_project.py --plugin my-plugin
uv sync
uv run poe build
```

`init_project.py` generates the root `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`, and `pyproject.toml` (whose poe tasks call the kit's build scripts directly), then scaffolds a first plugin that compiles, loads, and answers `!ping` out of the box. Add more with `uv run poe new-plugin <name>`.

Already have a CMake repo? Vendor the kit and declare a plugin with one call - `cs2_add_plugin` owns the module target, SDK glue, output layout, Metamod `.vdf`, and install rules:

```cmake
add_subdirectory(vendor/cs2-kit)
```

```cmake
# plugins/my-plugin/CMakeLists.txt
cs2_add_plugin(my-plugin)
```

Or write the skeleton yourself:

```cpp
#include <CS2Kit/Api.hpp>

struct Managers { ConfigManager Config; };
Managers& App();

class MyPlugin : public CS2Kit::PluginBase<Managers>
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return { .Name = "My Plugin", .Author = "me", .Version = "1.0.0", .LogTag = "MINE" };
    }

    bool OnLoad(bool late) override
    {
        return App().Config.Load("addons/my-plugin/configs/settings.jsonc");
    }
};

MyPlugin g_MyPlugin;
PLUGIN_EXPOSE(MyPlugin, g_MyPlugin);
Managers& App() { return MyPlugin::App(); }
```

A command is one self-registering aggregate - no dispatcher wiring, no arg parsing:

```cpp
static const bool _registered = CS2Kit::Registry<CS2Kit::CommandSpec>::Add({
    .Name = "slap",
    .Usage = "!slap <target>",
    .Permission = "s",
    .Args = {Target()},
    .Handler = [](CommandContext& c) {
        CS2Kit::PawnOps::Slap(c.Target->Controller());
        return c.Ok("cmd.slapped", {{"name", c.Target->GetName()}});
    },
});
```

The [Getting Started guide](https://suxrobgm.github.io/cs2-kit/) walks through the build setup and the rest.

## Documentation

Full guides and API reference: **[suxrobgm.github.io/cs2-kit](https://suxrobgm.github.io/cs2-kit/)**

- **Getting Started** - install, scaffold, build
- **Architecture** - services, policy, lifetimes
- **Plugin Base, Configuration, Commands, Menus, Players, Messages, SDK, Database, HTTP** - per-feature guides

## Contributing

Contributions are welcome. Since the API is still evolving, it's worth opening an issue to discuss larger changes first.

## License

[MIT](LICENSE)
