# VoltMod

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue)](https://voltygg.github.io/voltmod/)

VoltMod is a native C++23 framework for Counter-Strike 2 plugins on
Metamod:Source. It provides engine integration, load-cycle ownership, common
server services, and reproducible CMake and Conan builds. Plugins keep control
of permissions and game behavior. VoltMod does not host a scripting runtime.

> VoltMod is under active development. Public APIs may change between
> versions.

## What it includes

- One `Runtime` and deterministic cleanup per Metamod load cycle.
- Typed chat and console commands with targeting and injected permission policy.
- WASD center-HTML and clickable Panorama menus, including multi-step flows.
- Player tracking, translations, scheduled effects, and typed engine wrappers.
- Asynchronous HTTP and optional PostgreSQL with game-thread completions.
- JSONC configuration, startup diagnostics, and typed cross-plugin services.
- Project scaffolding, pinned build tools, tests, and server-ready install bundles.

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
`bootstrap` installs the Conan configuration, resolves dependencies, builds,
and runs the tests.

Later development uses:

```sh
uv run poe build
uv run poe test
uv run poe build windows-msvc-debug
uv run poe build-linux
uv run poe new-plugin fun-votes
```

`test` rebuilds before running CTest.

Output is written to
`build/<preset>/plugins/<name>/<platform-arch>/`. With `CS2_SERVER_PATH` set,
build straight into a local server and run it:

```sh
uv run poe build --install <name> --start
```

Then run `meta list` on the server console and test `!ping`.

See [Getting started](docs/getting-started.md) for the generated layout and
manual staging steps.

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

Add `cmd.slapped` to the translation files. `CommandManager` owns the command
for the load cycle. The plugin's `Runtime::Policy` decides permissions,
immunity, reply formatting, and broadcast behavior.

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

[Choosing a CS2 plugin framework](docs/framework-comparison.md) compares
VoltMod's native C++ model with SwiftlyS2, Plugify with S2SDK, and
CounterStrikeSharp using their official documentation.

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

Open an issue before starting a large change because the public API is still
evolving.

## License

VoltMod is available under the [MIT License](LICENSE).
