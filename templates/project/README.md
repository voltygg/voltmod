# $project

CS2 Metamod plugins built with
[VoltMod](https://github.com/voltygg/voltmod). Conan supplies the framework,
HL2SDK, and Metamod; no submodules are required.

## Setup

```sh
uv sync                # install CMake, Conan, Ninja, and the framework tasks
uv run poe bootstrap   # install Conan profiles and the remote, then build
```

Run `bootstrap` once, then use `uv run poe build` during development.

## Build

```sh
uv run poe build                              # release preset for this OS
uv run poe build windows-msvc-debug
uv run poe build-linux
ctest --preset windows-msvc-release
```

Plugin binaries land in `build/<preset>/plugins/<name>/<platform-arch>/`, and
`cmake --install build/<preset> --component <name> --prefix <dir>` stages a
server-ready `addons/` tree per plugin.

Without uv, drive Conan and CMake directly:

```sh
conan install . -pr:a linux-steamrt.txt -s build_type=Release \
  --output-folder build/linux-steamrt-release/generators --build=missing
cmake --workflow --preset linux-steamrt-release
```

## Add plugins

```sh
uv run poe new-plugin fun-votes
```

This creates `plugins/fun-votes/` with its CMake file, source, and configuration,
then registers it in the root `CMakeLists.txt`.

## PostgreSQL

Set `voltmod/*:with_postgres` to `True` in `conanfile.py` to add libpqxx and
the `VoltMod::Database` module.
