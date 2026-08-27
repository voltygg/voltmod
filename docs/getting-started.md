# Getting started {#getting_started}

[TOC]

This guide creates, builds, stages, and verifies a native Counter-Strike 2
plugin. The generated plugin works before you edit it and answers `!ping`.

## Prerequisites

- Git
- [uv](https://docs.astral.sh/uv/)
- Python 3.14 or newer
- A C++23 compiler: Visual Studio 2022+ on Windows or the Steam Runtime
  toolchain for Linux
- A CS2 dedicated server with Metamod:Source when you are ready to load it

The `voltmod` Python package pins CMake, Conan, Ninja, and clang-format, so
`uv sync` installs those tools. Conan downloads VoltMod, HL2SDK, and
Metamod:Source packages; generated projects have no submodules.

## Create a project

From an empty directory:

```sh
mkdir my-cs2-plugins
cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/voltmod.git voltmod init --plugin my-plugin
uv sync
```

The generator creates:

```text
CMakeLists.txt
CMakePresets.json
conanfile.py
pyproject.toml
plugins/
  my-plugin/
    CMakeLists.txt
    configs/
    src/
```

`CMakeLists.txt` loads VoltMod and registers the plugin. `CMakePresets.json`
contains matching configure, build, test, and workflow presets.
`conanfile.py` declares framework and test dependencies. `pyproject.toml`
provides the pinned tools and short Poe commands.

## Check the environment

```sh
uv run poe doctor
```

Doctor checks the project files, tools, compiler, Conan profiles, and package
remote without changing them. Before the first bootstrap, missing profiles or
the `volty` remote are expected warnings.

To include a server installation:

```sh
uv run poe doctor --server-path C:/cs2-server
```

## Bootstrap once

```sh
uv run poe bootstrap
```

Bootstrap:

1. Installs the canonical Conan profiles and public remote.
2. Selects `windows-msvc-release` or `linux-steamrt-release`.
3. Resolves the framework, HL2SDK, Metamod:Source, and project dependencies.
4. Honors an existing `conan.lock`.
5. Configures CMake, builds, and runs CTest.

Success ends with output under
`build/<preset>/plugins/my-plugin/<platform-arch>/`. Bootstrap is the first
build; use `uv run poe build` afterward, and `uv run poe test` when you
want CTest to run as well.

## Add another plugin

```sh
uv run poe new-plugin fun-votes
```

The command creates `plugins/fun-votes` and adds its `add_subdirectory` line
to the root project. It refuses to overwrite an existing directory.

Each scaffold includes:

- a `MetamodPlugin` lifecycle class;
- a load-cycle `App`;
- a `!ping` command;
- JSONC settings and schema;
- an English translation file;
- Git-backed build identity for `meta list`.

## Build and install

```sh
uv run poe build
uv run poe test
uv run poe build windows-msvc-debug
uv run poe build-linux
```

`test` brings the build up to date and then runs CTest.

Set `CS2_SERVER_PATH` to a CS2 dedicated server installation, in `.env` or the
environment, and install one plugin straight into it:

```sh
uv run poe build --install my-plugin --start
```

`--install` merges the server-ready `addons/` tree into `game/csgo`, seeds
`configs/settings.jsonc` without overwriting later edits, and `--start` launches
the server. `uv run poe install my-plugin` skips the build; `uv run poe
start-server` launches on its own.

To stage the same tree by hand instead:

```sh
cmake --install build/<preset> --component my-plugin --prefix dist
```

The install component contains:

```text
dist/addons/
  metamod/my-plugin.vdf
  my-plugin/
    bin/<platform>/my-plugin.<dll-or-so>
    configs/
  voltmod/
    gamedata/
```

Copy `dist/addons` into the server's `game/csgo` directory. Preserve
operator-edited settings when updating an existing installation.

## Verify on a server

Start the server and run:

```text
meta list
```

The plugin name, semantic version, commit, and build state should appear. Join
the server and enter `!ping`. A translated pong reply confirms lifecycle,
command, message, configuration, and translation setup.

## Where to edit

- `src/Plugin.cpp` defines identity and load/unload behavior.
- `src/App.cpp` builds the plugin's load-cycle object graph.
- `src/Commands.cpp` contains the example command.
- `src/Config.hpp` maps JSONC settings.
- `configs/settings.jsonc` contains operator settings.
- `configs/settings.schema.json` documents and validates settings.
- `configs/translations/*.json` contains player-facing text.

Add `.cpp` files anywhere below `src/`; `voltmod_add_plugin` discovers them.
Keep SDK-free decisions in plain types and add doctest coverage.

## Normal development loop

```sh
uv run poe build --install my-plugin
```

Restart the test server to pick up the new binary. Native modules stay locked
while loaded on Windows, so an install into a running server fails until it
stops.

Before publishing:

```sh
uv run poe lint
uv run poe format
uv run poe test
```

## Build details

`voltmod_add_plugin(my-plugin VERSION 1.0.0)` creates the C++23 Metamod
module, links the SDK and framework, enables the configured warning policy,
sets hidden symbol visibility, creates the build stamp and VDF, and defines the
install component.

Enable PostgreSQL in `conanfile.py`:

```python
default_options = {"voltmod/*:with_postgres": True}
```

Then request the feature:

```cmake
voltmod_add_plugin(my-plugin VERSION 1.0.0 FEATURES DATABASE)
```

For profiles, lockfiles, local package development, and existing CMake projects, see
@ref conan_guide.

## Api headers

`<VoltMod/Api.hpp>` gathers the core vocabulary, the `Runtime` facade, and
player/command/plugin plumbing - what a typical `OnLoad` and command handler
touches. Four more modules publish their own `Api.hpp`; include one where a
translation unit needs that surface:

| Header | Brings in |
|---|---|
| `<VoltMod/Api.hpp>` | Core vocabulary (`Event`, `Result`, `Subscription`, `Log`, `Scheduler`, `Translations`, `Capabilities`, ...), `Runtime`, `Player`/`PlayerManager`/`Policy`, commands, `ChatColors`, `MetamodPlugin` and `LoadStandardConfig` |
| `<VoltMod/Entities/Api.hpp>` | Entity/Pawn/Controller wrappers, `EntitySystem`, `EntityOps`, `Items`, and `ConVar`/`ConVarLease` |
| `<VoltMod/Hooks/Api.hpp>` | The per-tick hooks (`Movement`, `Teleport`, ...), game events, and messaging |
| `<VoltMod/Menu/Api.hpp>` | `MenuBuilder`, `Flow`, presets, and every option type |
| `<VoltMod/Unsafe/Api.hpp>` | Raw interfaces, gamedata, `MemoryAccess`, and vtable hooking - opt in only where you need it |
| `<VoltMod/Database/Api.hpp>` | The PostgreSQL vocabulary (see @ref database_guide) |

None of these reach nlohmann. A plugin's own `Config.hpp` includes
`<VoltMod/App/Config.hpp>` for `JsonConfig` and `StandardPluginSettings` (see
@ref config_guide) - most translation units never touch settings JSON, so it
stays out of the umbrella.

## Next steps

- @ref plugin_guide - lifecycle and ownership
- @ref commands_guide - commands and targeting
- @ref config_guide - settings and validation
- @ref menus_guide - menus and multi-step flows
- @ref testing_guide - SDK-free unit tests
- @ref framework_comparison - compare development models
