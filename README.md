# VoltMod

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://voltygg.github.io/voltmod/)

VoltMod is a native C++23 framework for Counter-Strike 2 plugins on
Metamod:Source. It owns the repeated engine integration, plugin lifecycle,
build configuration, and common server services while each plugin keeps
control of its policy and game behavior.

Choose VoltMod when you want native C++, explicit ownership, and reproducible
CMake and Conan builds. It does not host C# or another scripting runtime.

> VoltMod is under active development. Public APIs may change between
> versions.

## What it includes

- A managed Metamod lifecycle with one `Runtime` per load cycle.
- Declarative console and chat commands with typed arguments, targeting,
  permissions, replies, and broadcasts.
- WASD center-HTML or clickable Panorama menus, context-aware rows, reusable player pickers, and
  multi-step flows.
- Player tracking, actions, scheduled effects, translations, and chat colors.
- Typed wrappers for entities, schemas, events, convars, user messages,
  typed gamedata bindings, hooks, movement commands, input history, precaching, and transmit
  filters.
- Runtime-scoped subscriptions and cleanup instead of process-lifetime plugin
  globals.
- Asynchronous HTTP with game-thread completions.
- Optional asynchronous PostgreSQL, migrations, and row mapping.
- JSONC configuration, startup validation, status sections, build identity, and
  typed cross-plugin services.
- Project and plugin scaffolding, pinned build tools, CMake presets, tests,
  package publishing, and server-ready install components.

VoltMod supplies infrastructure rather than an admin model. A plugin injects
its own permission and immunity policy into the shared command, targeting,
menu, and message pipelines.

## Create a project

You need Git, [uv](https://docs.astral.sh/uv/), Python 3.14+, and a C++23
compiler. Windows uses Visual Studio 2022 or newer. Linux uses the supplied
Steam Runtime profiles.

From an empty directory:

```sh
mkdir my-cs2-plugins
cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/voltmod.git voltmod init --plugin my-plugin
uv sync
uv run poe doctor
uv run poe bootstrap
```

The generated `my-plugin` is registered automatically and answers `!ping`.
Bootstrap installs the Conan configuration, resolves dependencies, configures
CMake, builds, and runs tests. It is already the first build.

Later development uses:

```sh
uv run poe build
uv run poe test
uv run poe build windows-msvc-debug
uv run poe build-linux
uv run poe new-plugin fun-votes
```

Tests are a separate command so the edit-build loop stays fast; `poe test`
recompiles first, then runs them.

Output is written to
`build/<preset>/plugins/<name>/<platform-arch>/`. With `CS2_SERVER_PATH` set,
build straight into a local server and run it:

```sh
uv run poe build --install <name> --start
```

Then run `meta list` on the server console and test `!ping`.

The [getting-started guide](docs/getting-started.md) explains every generated
file, expected result, and common failure.

## Write a command

The handler's parameter list is the argument spec: every argument is parsed,
resolved and immunity-checked before the handler runs.

```cpp
namespace Args = VoltMod::Args;

runtime.Commands.Add("slap")
    .Describe("Slap a player.")
    .Permission("admin.slap")
    .Run([&runtime](VoltMod::Caller c, Args::Target t)
             -> VoltMod::Result<VoltMod::Reply> {
        runtime.World.Pawns.Slap(t.Value->Ctrl());
        return c.Ok("cmd.slapped", {{"name", t.Value->Name()}});
    });
```

Add `cmd.slapped` to the translation files. `Run` returns a `Subscription` that
unregisters the command when it is dropped. The plugin's `Runtime::Policy`
decides permissions, immunity, reply formatting, and broadcast behavior.

## Add VoltMod to an existing project

Require the Conan package:

```python
requires = ("voltmod/[~1.3]",)
```

Load it and declare plugins:

```cmake
find_package(voltmod CONFIG REQUIRED)
add_subdirectory(plugins/my-plugin)
```

```cmake
# plugins/my-plugin/CMakeLists.txt
voltmod_add_plugin(my-plugin VERSION 1.0.0)
```

The helper configures the native module, SDK glue, output layout, generated VDF,
build stamp, and install component. Enable PostgreSQL in the consumer recipe:

```python
default_options = {"voltmod/*:with_postgres": True}
```

Then request it from the plugin:

```cmake
voltmod_add_plugin(my-plugin VERSION 1.0.0 FEATURES DATABASE)
```

See [Consuming VoltMod with Conan](docs/consuming-via-conan.md) for profiles,
local package development, remotes, and lockfiles.

## Compare frameworks

| Framework | Plugin language | Runtime model | Built-in strengths |
| --- | --- | --- | --- |
| **VoltMod** | C++23 | Native Metamod modules | Typed command/targeting pipeline, WASD flows, explicit load-cycle ownership, async HTTP/PostgreSQL, and CMake/Conan scaffolding |
| [SwiftlyS2](https://swiftlys2.net/) | C# on .NET 10 | Managed plugins over a C++ core | Broad Source 2 services, advanced menus, database connections, dependency injection, and automatic hot reload |
| [Plugify](https://plugify.net/) with S2SDK | C++, C#, Python, Go, Lua, Rust, JavaScript/TypeScript, and more | Multi-language modules with Source 2 supplied by its Metamod/S2SDK stack | Cross-language calls, language modules, package management, and low-level Source 2 APIs |
| [CounterStrikeSharp](https://docs.cssharp.dev/) | C# on .NET 8 | Managed scripting layer hosted by Metamod | Familiar .NET workflow, commands/events/listeners, schema access, localization, capabilities, and automatic DLL hot reload |

This is a workflow summary, not a performance ranking. See
[Choosing a CS2 plugin framework](docs/framework-comparison.md) for the
feature-by-feature matrix, scope notes, and official sources.

## Documentation

The generated guides and API reference are published at
[voltygg.github.io/voltmod](https://voltygg.github.io/voltmod/).

- [Getting started](docs/getting-started.md)
- [Framework comparison](docs/framework-comparison.md)
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

Open an issue before starting a large change. Early discussion helps while the
public API is evolving.

## License

VoltMod is available under the [MIT License](LICENSE).
