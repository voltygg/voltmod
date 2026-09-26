# Getting started {#getting_started}

[TOC]

## Prerequisites

- Git
- [uv](https://docs.astral.sh/uv/) and Python 3.14 or newer
- A C++23 compiler: Visual Studio 2022 or newer on Windows, the Steam Runtime toolchain on Linux
- A CS2 dedicated server to load the plugin into

`uv sync` installs the `voltmod` Python package, which pins CMake, Conan, Ninja and clang-format.
Conan fetches VoltMod, HL2SDK and KHook; a generated project has no submodules.

## Create a project

```sh
mkdir my-cs2-plugins && cd my-cs2-plugins
git init
uvx --from git+https://github.com/voltygg/voltmod.git voltmod new project --plugin my-plugin
uv sync
```

`voltmod new project` writes `CMakeLists.txt`, `CMakePresets.json`, `conanfile.py`, `pyproject.toml` and
`plugins/my-plugin/`. The generated plugin builds as it is and answers `!ping`. See
@ref plugin_guide for what is in it.

Add more plugins later:

```sh
uv run poe new-plugin fun-votes
```

That creates `plugins/fun-votes/` and adds `add_subdirectory(plugins/fun-votes)` to the root
`CMakeLists.txt`. It refuses to overwrite an existing directory.

## Check the environment

```sh
uv run poe doctor
uv run poe doctor --server C:/cs2-server
```

Doctor reports on tools, compiler, Conan profiles and the package remote without changing
anything. Before the first bootstrap, missing profiles and a missing `volty` remote are expected.

## Build

```sh
uv run poe bootstrap   # first time: Conan profiles and remote, then configure, build and test
uv run poe build       # afterwards
uv run poe test        # build, then CTest
```

Presets are `windows-msvc-release`, `windows-msvc-debug`, `linux-steamrt-release` and
`linux-steamrt-debug`; `uv run poe build -p windows-msvc-debug` picks one. Binaries land in
`build/<preset>/plugins/<name>/<platform-arch>/`. Profiles, lockfiles and the CMake functions are
in @ref conan_guide.

## Install into a server

Point `CS2_SERVER_PATH` at a CS2 dedicated server root, in `.env` or the environment:

```sh
uv run poe run my-plugin
```

`run` builds, then installs the host and the plugin into `game/csgo`, copying each
file under `configs/` only when the server does not already have one, so operator edits survive.
Then it adds `Game csgo/addons/voltmod` above `Game csgo` in `gameinfo.gi` if it is missing, and
launches the server. With no plugin named it installs every plugin in the repo. To do either step
on its own:

```sh
uv run poe install my-plugin
uv run poe serve
```

The host is installed with the plugin, from the same VoltMod build: a plugin built against a
different host is refused. @ref plugin_guide has the resulting `addons/` layout.

Windows keeps a loaded module locked, so an install into a running server fails until the file is
released. Either restart it, or run `volt unload my-plugin` on the console, install, then
`volt load my-plugin`.

## Load it

On the server console:

```text
volt list
```

The host prints each loaded plugin with its version and description, in load order. Join and
enter `!ping`. @ref host_guide covers the rest of the `volt` commands and what to do when a plugin
does not appear.
