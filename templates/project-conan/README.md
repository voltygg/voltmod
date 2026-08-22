# $project

CS2 Metamod plugins built on [cs2-kit](https://github.com/voltygg/cs2-kit),
consumed as a Conan package - no submodules.

## One-time setup

```sh
# The kit's canonical Conan profiles (linux-steamrt, windows-msvc), by name:
conan config install https://github.com/voltygg/cs2-kit.git -sf conan

# The private remote (ask a maintainer for a read entitlement token):
conan remote add voltygg https://conan.cloudsmith.io/voltygg/cs2-kit/
conan remote login voltygg voltygg/cs2-kit -p <token>
```

## Build

```sh
# Linux (SteamRT-compatible toolchain, gcc-14):
conan install . -pr:a linux-steamrt -s build_type=Release \
  --output-folder build/linux-steamrt-release/generators -r voltygg -r conancenter
cmake --workflow --preset linux-steamrt-release

# Windows (MSVC, static runtime):
conan install . -pr:a windows-msvc -s build_type=Release -s compiler.runtime_type=Release \
  --output-folder build/windows-msvc-release/generators -r voltygg -r conancenter
cmake --workflow --preset windows-msvc-release
```

Plugin binaries land in `build/<preset>/plugins/<name>/<platform-arch>/`, and
`cmake --install build/<preset> --component <name> --prefix <dir>` stages a
server-ready `addons/` tree per plugin.

## Adding plugins

Each plugin is a directory under `plugins/<name>/` with a one-line
`CMakeLists.txt` (`cs2_add_plugin(<name>)`) plus its `src/` and `configs/`,
registered with `add_subdirectory(plugins/<name>)` in the root CMakeLists.
The cs2-kit package ships the scaffold tree under `templates/plugin/` if you
want a starting point.

## Postgres

Set `cs2-kit/*:with_postgres=True` in the conanfile options (already listed,
default False) - libpqxx arrives transitively and the kit's `CS2Kit::Database`
module lights up.
