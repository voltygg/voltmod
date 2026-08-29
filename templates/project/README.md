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

`doctor` checks the environment without changing it. `bootstrap` installs the
Conan profiles and remote, resolves dependencies, builds, and runs the tests.

Bootstrap is already the first build. Use this afterward:

```sh
uv run poe build
uv run poe test
```

`test` rebuilds before running CTest.

## Verify $plugin

Point `CS2_SERVER_PATH` at a CS2 dedicated server installation, in `.env` or
the environment, then build straight into it:

```sh
uv run poe build --install $plugin --start
```

`--install` merges the server-ready `addons/` tree into `game/csgo` without
overwriting edited settings. `--start` then launches the server. Run `meta list`,
confirm `$plugin` appears, join, and enter `!ping`.

To install without rebuilding, or to launch on its own:

```sh
uv run poe install $plugin
uv run poe start-server
```

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
| `uv run poe build` | Build the release preset for this OS |
| `uv run poe test` | Build, then run the test suite |
| `uv run poe build windows-msvc-debug` | Build Windows debug |
| `uv run poe build-linux` | Build Linux Steam Runtime release |
| `uv run poe build --install <name> --start` | Build, install locally, and launch the server |
| `uv run poe install [name]` | Install built plugins into the local server |
| `uv run poe start-server` | Launch the local CS2 dedicated server |
| `uv run poe new-plugin <name>` | Scaffold and register another plugin |
| `uv run poe format` | Apply the pinned C++ formatting |

## PostgreSQL

Set `voltmod/*:with_postgres` to `True` in `conanfile.py`, then add
`FEATURES DATABASE` to the plugin's `voltmod_add_plugin` call.
