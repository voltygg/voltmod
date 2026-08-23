# $project

CS2 Metamod plugins built on [cs2-kit](https://github.com/voltygg/cs2-kit).
Everything - the kit, the HL2SDK and Metamod - arrives as a Conan package. No
submodules.

## Setup

```sh
uv sync                # provisions CMake/Conan/Ninja and the kit's build tooling
uv run poe bootstrap   # installs the Conan profiles + remote, then builds
```

`bootstrap` is a one-off; after it, `uv run poe build` is the loop.

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

## Adding plugins

```sh
uv run poe new-plugin fun-votes
```

That stamps `plugins/fun-votes/` (a one-line `CMakeLists.txt` calling
`cs2_add_plugin`, plus `src/` and `configs/`) and registers its
`add_subdirectory()` in the root CMakeLists.

## Postgres

Flip `cs2-kit/*:with_postgres` to `True` in `conanfile.py` - libpqxx arrives
transitively and the kit's `CS2Kit::Database` module lights up.
