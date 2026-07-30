# $project

CS2 Metamod plugins built on [cs2-kit](https://github.com/voltygg/cs2-kit).

## Requirements

- [uv](https://docs.astral.sh/uv/) (provisions CMake, Conan, Ninja, and the linters)
- Windows: Visual Studio 2026 Build Tools with the C++ x64 workload
- Linux: gcc-14 (Steam Runtime 3 "Sniper" toolchain)

## Commands

```sh
uv sync                          # one-time: provision the build toolchain
uv run poe build                 # conan install + cmake workflow (release, this OS)
uv run poe build windows-msvc-debug
uv run poe build-linux
uv run poe new-plugin <name>     # scaffold another plugin under plugins/
uv run poe format                # clang-format plugins/
uv run poe lint                  # ruff over the repo's python
```

Build output lands in `build/<preset>/plugins/<name>/<platform-arch>/`. A plugin's
deploy bundle (module + metamod .vdf + configs + shared gamedata) is produced by
`cmake --install build/<preset> --component <name> --prefix <dir>`.

## Pin dependencies (optional)

Once the first build works, lock the third-party dependency graph so all machines
resolve identical packages:

```sh
uv run conan lock create . --profile:host vendor/cs2-kit/conan/profiles/windows-msvc.txt --profile:build vendor/cs2-kit/conan/profiles/windows-msvc.txt
```

(use `linux-steamrt.txt` on Linux). Commit the resulting `conan.lock`; builds pick
it up automatically.

## Postgres

Uncomment `set(CS2KIT_ENABLE_POSTGRES ON)` in `CMakeLists.txt` and the
`libpqxx/7.10.0` line in `conanfile.py` to enable cs2-kit's async Postgres client.
