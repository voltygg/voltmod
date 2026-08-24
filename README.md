# VoltMod

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://voltygg.github.io/voltmod/)

VoltMod is a native C++23 framework for building Counter-Strike 2 server
plugins on Metamod:Source 2.0. It handles the repeated engine integration,
plugin lifecycle, build setup, and common server systems so plugin code can
focus on game behavior.

VoltMod is designed for developers who want native C++, explicit ownership, and
reproducible CMake and Conan builds. It does not impose an admin or permissions
model: each plugin supplies its own policy while using the same command,
targeting, menu, message, and effect pipelines.

> **Work in progress:** the API is still evolving and may change between
> versions.

## Features

- **Managed plugin lifecycle** - `MetamodPlugin` owns the Metamod entry points,
  standard hooks, player tracking, frame pump, and one `Runtime` for each load
  cycle.
- **Declarative commands** - define a `CommandSpec` with aliases, permissions,
  surfaces, typed arguments, and a handler. VoltMod validates and resolves
  input before the handler runs.
- **Player targeting** - resolve selectors such as `@all`, `@me`, `@t`, `@ct`,
  `@dead`, `@random`, `#slot`, SteamIDs, and name fragments through one
  policy-aware pipeline.
- **WASD menus and flows** - build center-HTML menus from typed options,
  context-aware rows, reusable pickers, and multi-step `Flow` workflows.
- **Players, actions, and effects** - track connected players, keep per-slot
  state, dispatch target actions, and manage scheduled effects.
- **Messages and translations** - send chat, center, center-HTML, and alert
  messages with per-player translations, token replacement, and chat colors.
- **Engine wrappers** - use entities, schema fields, game events, convars,
  user messages, gamedata, precaching, transmit filters, pawn operations,
  client convars, movement hooks, and input history through typed APIs.
- **Lifecycle-safe cleanup** - runtime-scoped services and RAII
  subscriptions keep callbacks, hooks, and plugin state within one
  load/unload cycle.
- **Asynchronous HTTP** - issue requests off the game thread and receive
  completions on the game thread, with JSON endpoint helpers for configured
  APIs.
- **Optional PostgreSQL** - enable an async-first libpqxx client, forward-only
  migrations, and column-table mapping for common insert, select, and row
  parsing work.
- **Configuration and diagnostics** - load JSONC settings, validate startup
  stages, publish plugin identity, expose status sections, and exchange typed
  interfaces with other plugins.
- **Project tooling** - scaffold projects and plugins, build with pinned tools,
  generate VDF and manifest files, stamp Git build information, run tests, and
  stage server-ready addon bundles.

## Quick start

You need Git, [uv](https://docs.astral.sh/uv/), and a C++23 toolchain. On
Windows, use Visual Studio 2022 or newer with the C++ workload. Linux builds use
the supplied Steam Runtime profile.

Create a project in an empty directory:

```sh
mkdir my-cs2-plugins
cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/voltmod.git voltmod init --plugin my-plugin
uv sync
uv run poe bootstrap
```

### What bootstrap does

`uv run poe bootstrap` runs the generated Poe task, which forwards to
`voltmod bootstrap` in the project directory:

1. Conan installs VoltMod's profiles and public package remote. A VoltMod
   checkout uses its local `conan/` directory; a generated consumer project
   downloads that directory from the VoltMod repository.
2. VoltMod selects the release preset for the current OS:
   `windows-msvc-release` or `linux-steamrt-release`.
3. Conan resolves the framework, HL2SDK, Metamod:Source, and project
   dependencies. Missing packages are built when allowed, and an existing
   `conan.lock` is honored.
4. CMake runs the selected workflow preset: configure, build, then test.

The command updates the user's Conan configuration and cache and writes local
build output under `build/<preset>/`. It does not copy a plugin to a CS2 server.
After the first successful bootstrap, use `uv run poe build` for the normal
development loop.

The generated project includes CMake presets, a Conan recipe, pinned build
tools, and a working `my-plugin` that answers `!ping`.

After the first build, use:

```sh
uv run poe build
uv run poe build windows-msvc-debug
uv run poe build-linux
uv run poe new-plugin fun-votes
```

Build output is written to
`build/<preset>/plugins/<name>/<platform-arch>/`. To create a server-ready
`addons/` tree, install one plugin component:

```sh
cmake --install build/<preset> --component <name> --prefix dist
```

The framework, HL2SDK, and Metamod:Source are resolved as Conan packages. The
bootstrap task installs VoltMod's Conan profiles and public remote before
building.

## Add VoltMod to an existing project

Require VoltMod in `conanfile.py`:

```python
requires = ("voltmod/[~1]",)
```

Load the package and declare each plugin:

```cmake
find_package(voltmod CONFIG REQUIRED)
add_subdirectory(plugins/my-plugin)
```

```cmake
# plugins/my-plugin/CMakeLists.txt
voltmod_add_plugin(my-plugin VERSION 1.0.0)
```

`voltmod_add_plugin` discovers `src/*.cpp` by default and configures the C++23
Metamod module, HL2SDK sources, precompiled header, output layout, build stamp,
manifest, VDF, and install component.

Enable the optional database component in the consumer recipe:

```python
default_options = {"voltmod/*:with_postgres": True}
```

Then request it from the plugin target:

```cmake
voltmod_add_plugin(my-plugin VERSION 1.0.0 FEATURES DATABASE)
```

See [Consuming VoltMod with Conan](docs/consuming-via-conan.md) for profiles,
editable packages, lockfiles, and package details.

## Write a command

The generated plugin registers commands from its load-cycle `App`. A command is
plain data, and required arguments are resolved before its handler runs:

```cpp
runtime.Commands.Register({
    .Name = "slap",
    .Description = "Slap a player.",
    .Permission = "admin.slap",
    .Args = {VoltMod::Target()},
    .Handler = [](VoltMod::CommandContext& context) {
        VoltMod::PawnOps::Slap(context.Target().Controller());
        return context.Ok("cmd.slapped", {{"name", context.Target().GetName()}});
    },
});
```

Add `cmd.slapped` to the plugin's translation files for the localized reply.
Permissions, immunity, replies, and broadcasts come from the plugin's injected
`Runtime::Policy`. VoltMod supplies the pipeline without defining the server's
admin model.

## How VoltMod compares

These frameworks overlap, but their programming and deployment models differ.
This table is a workflow comparison, not a performance ranking.

| Framework | Plugin model | Main emphasis | Consider it when |
| --- | --- | --- | --- |
| **VoltMod** | Native C++23 Metamod modules built with CMake and Conan | Explicit lifetimes, declarative commands and menus, typed engine APIs, async services, and project scaffolding | You want native C++ and direct control of ownership without rebuilding common Metamod and HL2SDK plumbing |
| [**SwiftlyS2**](https://github.com/swiftly-solution/swiftlys2) | C# plugins backed by a C++ core | A broad Source 2 surface covering commands, convars, entities, typed events, hooks, net messages, menus, and scheduling | You want a C# plugin API backed by native engine integration |
| [**Plugify**](https://plugify.net/) | Language modules for C++, C#, Python, Go, Lua, Rust, JavaScript/TypeScript, and others; Source 2 support through its Metamod and S2SDK stack | Multi-language loading, manifests, packages, and cross-language calls | You need several plugin languages or interoperability between them |
| [**CounterStrikeSharp**](https://docs.cssharp.dev/) | C# on a .NET 8 scripting layer hosted by a Metamod plugin | C# commands, events, timers, listeners, schema access, hot reload, and its plugin ecosystem | You want a .NET workflow and to build against the CounterStrikeSharp API |

VoltMod is not API-compatible with the other frameworks and does not host C# or
other scripting languages. Choose it when the plugin itself should be a native
C++ Metamod module with framework-managed infrastructure.

## Documentation

The full guides and API reference are available at
[voltygg.github.io/voltmod](https://voltygg.github.io/voltmod/).

- [Getting started](docs/getting-started.md)
- [Architecture and lifetimes](docs/architecture.md)
- [Plugin lifecycle](docs/plugin.md)
- [Commands and targeting](docs/commands.md)
- [Menus and flows](docs/menu.md)
- [Players, actions, and effects](docs/players.md)
- [Messages and chat](docs/chat.md)
- [Configuration](docs/config.md)
- [SDK wrappers](docs/sdk.md)
- [PostgreSQL](docs/database.md)
- [HTTP](docs/http.md)
- [Testing](docs/testing.md)

## Contributing

Open an issue before starting a large change. The API is still evolving, so
early discussion helps avoid work against a changing contract.

## License

VoltMod is available under the [MIT License](LICENSE).
