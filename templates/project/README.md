# $project

Counter-Strike 2 Metamod plugins built with
[VoltMod](https://github.com/voltygg/voltmod).

## First build

Install Git, [uv](https://docs.astral.sh/uv/), Python 3.14+, and a C++23
compiler. Then run:

```sh
uv sync
uv run poe doctor
uv run poe bootstrap
```

Doctor checks the environment without changing it. Bootstrap installs the
Conan profiles and package remote, resolves VoltMod, HL2SDK, and Metamod,
configures CMake, builds, and runs tests.

Bootstrap is already the first build. Use this afterward:

```sh
uv run poe build
```

## Verify $plugin

Stage a server-ready addon:

```sh
cmake --install build/<preset> --component $plugin --prefix dist
```

Copy `dist/addons` into the CS2 server's `game/csgo` directory. Start the
server, run `meta list`, and confirm `$plugin` appears. Join and enter `!ping`
to verify command and translation loading.

Build output is under
`build/<preset>/plugins/<name>/<platform-arch>/`.

## Add a plugin

```sh
uv run poe new-plugin fun-votes
```

The command creates `plugins/fun-votes` with source and configuration files,
then registers it in the root `CMakeLists.txt`.

## Common commands

| Command | Purpose |
| --- | --- |
| `uv run poe doctor` | Check tools and project configuration |
| `uv run poe build` | Build and test the release preset for this OS |
| `uv run poe build windows-msvc-debug` | Build and test Windows debug |
| `uv run poe build-linux` | Build and test Linux Steam Runtime release |
| `uv run poe new-plugin <name>` | Scaffold and register another plugin |
| `uv run poe format-check` | Check C++ formatting |

## PostgreSQL

Set `voltmod/*:with_postgres` to `True` in `conanfile.py`, then add
`FEATURES DATABASE` to the plugin's `voltmod_add_plugin` call.
