# $project

Counter-Strike 2 server plugins built with
[VoltMod](https://github.com/voltygg/voltmod). Each plugin lives in `plugins/<name>/` and is
loaded by the VoltMod host.

## First build

Install Git, [uv](https://docs.astral.sh/uv/), Python 3.14+ and a C++23 compiler, then:

```sh
uv sync
uv run poe doctor
uv run poe bootstrap
```

`doctor` reports on the environment without changing it. `bootstrap` installs the Conan profiles
and remote, resolves dependencies, builds and runs the tests, so it is also the first build.

## Commands

| Command | Does |
| --- | --- |
| `uv run poe build` | Build the release preset for this OS |
| `uv run poe build -p windows-msvc-debug` | Build another preset |
| `uv run poe build-linux` | Build the Linux Steam Runtime release |
| `uv run poe test` | Build, then run CTest |
| `uv run poe run [name]` | Build, install into the local server, launch it |
| `uv run poe install [name]` | Install already-built plugins into the local server |
| `uv run poe serve` | Launch the local CS2 dedicated server |
| `uv run poe new-plugin <name>` | Scaffold and register another plugin |
| `uv run poe lint` | Lint the tooling and check the plugin source conventions |
| `uv run poe format` | Apply the pinned C++ formatting |
| `uv run poe doctor` | Check tools and project configuration |

Set `CS2_SERVER_PATH` to a CS2 dedicated server root, in `.env` or the environment, before
installing. The install merges the host and the plugins into `game/csgo` and copies each
file under a plugin's `configs/` only when the server has none, so operator edits survive. Verify with
`volt list` on the server console.

Build output is under `build/<preset>/plugins/<name>/<platform-arch>/`.

## Add a plugin

```sh
uv run poe new-plugin fun-votes
```

That creates `plugins/fun-votes/` with its sources, `plugin.json` and configuration, then adds
`add_subdirectory(plugins/fun-votes)` to the root `CMakeLists.txt`. Name it, version it and
declare what it depends on in its `plugin.json`.

For a plugin that needs a database, add `DATABASE` to its `voltmod_add_plugin` call; the
driver (PostgreSQL, MariaDB or SQLite) is chosen at run time from its settings.
