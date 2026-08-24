# $project

CS2 Metamod plugins built on [VoltMod](https://github.com/voltygg/voltmod).
The framework, HL2SDK, and Metamod arrive as Conan packages. No submodules are
required.

## Setup

```sh
uv sync                # install CMake, Conan, Ninja, and the framework tasks
uv run poe bootstrap   # install Conan profiles and the remote, then build
```

`bootstrap` runs once. After it, use `uv run poe build` for the normal build loop.

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

This stamps `plugins/fun-votes/` (a one-line `CMakeLists.txt` calling
`voltmod_add_plugin`, plus `src/` and `configs/`) and registers its
`add_subdirectory()` in the root CMakeLists.

## PostgreSQL

Flip `voltmod/*:with_postgres` to `True` in `conanfile.py` - libpqxx arrives
transitively and the framework's `VoltMod::Database` module lights up.
