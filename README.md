# CS2Kit

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Docs](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://suxrobgm.github.io/cs2-kit/)

A C++23 toolkit for building Counter-Strike 2 server plugins on Metamod:Source 2.0.

You describe your plugin's behavior as data - commands, menu rows, effects, database rows - and CS2Kit owns the machinery around it: engine setup, hooks, player tracking, permission gating, message transport, and teardown.

> **Work in progress.** The API is still evolving and may change between versions.

## What you get

- **Plugin base** - subclass `MetamodPlugin` and the Metamod lifecycle, standard hooks and player tracking are wired up; you get a `Runtime` holding every kit service for the duration of a load, and cleanup is ordinary destruction.
- **Declarative commands** - a `CommandSpec` is plain data; targets, durations, and SteamIDs are resolved and validated before your handler runs, with localized error replies.
- **Target selectors** - `@all`, `@me`, `@t`, `@ct`, `@dead`, `@random`, `#slot`, SteamIDs, and name fragments, with immunity applied through your injected policy.
- **Menus** - WASD-navigated in-game menus: typed rows, policy-aware context rows for admin/target actions, ready-made pickers, and the `Flow` wizard for multi-step "pick duration → pick reason → confirm" chains.
- **One policy hook** - permissions, immunity, replies, and broadcasts are injected once via `Runtime::Policy`; the kit ships no admin model of its own.
- **Messages** - one service for chat, center print, center-HTML, and alerts, with per-player translations (`ReplyKey`) and chat colors.
- **Typed game events** - `Listen<PlayerDeath>(...)` instead of string names and `GetInt` calls; the raw overload stays for unmodeled events.
- **PostgreSQL** (optional) - async-first client (worker thread owns the connection, completions on the game thread), column-table row mapping that generates the INSERT/SELECT/parse code, and a migration runner. Gated behind `CS2KIT_ENABLE_POSTGRES`.
- **HTTP** - async requests with game-thread completions, plus config-driven JSON endpoint helpers.
- **Project + plugin scaffolding** - `cs2kit init` stamps a complete buildable project (root CMake, presets, conanfile, poe tasks); `cs2kit new-plugin` adds more from the `templates/plugin/` tree.
- **One-call plugin builds** - `cs2_add_plugin(<name>)` declares the whole Metamod module: sources, SDK glue, output layout, the generated `.vdf`, and per-plugin install components.

## Quick start

Start a plugin project from an empty directory:

```sh
mkdir my-cs2-plugins && cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/cs2-kit.git cs2kit init --plugin my-plugin
uv sync
uv run poe bootstrap
```

That generates the root `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`, and `pyproject.toml`, then scaffolds a first plugin that compiles, loads, and answers `!ping` out of the box. Add more with `uv run poe new-plugin <name>`.

The kit, the HL2SDK and Metamod all arrive as Conan packages from a public remote - no submodules, nothing to build but your own code. See `docs/consuming-via-conan.md`.

Already have a CMake repo? Add the requirement and declare a plugin with one call - `cs2_add_plugin` owns the module target, SDK glue, output layout, Metamod `.vdf`, and install rules:

```python
# conanfile.py
requires = ("cs2-kit/[~1]",)
```

```cmake
find_package(cs2-kit CONFIG REQUIRED)
```

```cmake
# plugins/my-plugin/CMakeLists.txt
cs2_add_plugin(my-plugin)
```

Or write the skeleton yourself:

```cpp
#include <CS2Kit/Api.hpp>

struct App
{
    explicit App(CS2Kit::Runtime& runtime) : Runtime(runtime) {}
    CS2Kit::Runtime& Runtime;
    ConfigManager Config;
};

class MyPlugin final : public CS2Kit::MetamodPlugin
{
protected:
    CS2Kit::PluginInfo Info() const override
    {
        return { .Name = "My Plugin", .Author = "me", .Version = "1.0.0", .LogTag = "MINE" };
    }

    bool OnLoad(CS2Kit::Runtime& runtime, bool late) override
    {
        _app.emplace(runtime);
        return _app->Config.Load("addons/my-plugin/configs/settings.jsonc");
    }

    void OnUnload() override { _app.reset(); }

private:
    std::optional<App> _app;
};

CS2KIT_PLUGIN(MyPlugin);
```

A command is one aggregate - no dispatcher wiring, no arg parsing:

```cpp
runtime.Commands.Register({
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
