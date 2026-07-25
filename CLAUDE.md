# CS2Kit - C++23 CS2 Plugin Development Library

Reusable C++23 library for building Counter-Strike 2 server plugins with
Metamod:Source 2.0.

## Tech Stack

- Language: C++23
- Framework: Metamod:Source 2.0 + hl2sdk-cs2
- Build: CMake 4.3.4+ presets + Conan 2.29.1+
- Public target: `CS2Kit::CS2Kit`
- Docs: Doxygen + doxygen-awesome-css (`docs/`)

## Project Structure

```text
include/CS2Kit/        Public API headers
src/                   Implementation
gamedata/              Engine signatures and offsets
cmake/                 CS2KitSdk.cmake + CS2Plugin.cmake (cs2_add_plugin)
                       + CS2Tests.cmake (cs2_add_tests) + DoctestMain.cpp + plugin.vdf.in
                       + CS2KitBuildInfo.cmake (git/version stamping -> <CS2Kit/BuildInfo.hpp>)
scripts/               Build tooling (cwd-based; consumers invoke them directly),
                       init_project.py + new_plugin.py scaffold generators
templates/plugin/      Plugin scaffold tree ($name/$ns/... placeholders)
templates/project/     Consumer-project scaffold tree ($project placeholder)
tests/                 SDK-free unit tests (doctest + ctest); see docs/testing.md
docs/                  Doxygen pages and guides
vendor/                SDK submodules
CMakeLists.txt         Standalone CMake build
CMakePresets.json      Windows/Linux presets (consumers include this file)
conanfile.py           Third-party deps
conan/profiles/        Conan profiles (canonical; consuming repos reuse them)
```

`templates/project/CMakePresets.json` includes the root `CMakePresets.json` by
relative path; preset names are public API for consumers - rename with care.

## Build Commands

```bash
uv run poe build
uv run poe build windows-msvc-release
uv run poe build-linux
uv run poe new-plugin <name>   # scaffold a plugin into the invoking repo's plugins/
python scripts/init_project.py # stamp a whole consumer project (run from its root)
```

`poe build` runs the full workflow preset (configure, build, ctest). All scripts
target `Path.cwd()`, so consuming repos call them in place
(`python vendor/cs2-kit/scripts/build.py`) instead of keeping wrappers.

Consuming projects add the subdirectory and declare plugins with the kit-provided
`cs2_add_plugin` (from `cmake/CS2Plugin.cmake`, included by the kit's root
CMakeLists):

```cmake
add_subdirectory(vendor/cs2-kit)
```

```cmake
# plugins/<name>/CMakeLists.txt
cs2_add_plugin(<name> [SOURCES ...] [INCLUDE_DIRS ...] [LIBRARIES ...])
```

## Code Conventions

- C++23.
- `.hpp` headers, not `.h`.
- C#-style naming: `PascalCase` types/methods, `_camelCase` members.
- Service container + `Engine()` accessor; no process-lifetime singletons.
- Use `std::format`, designated initializers, and `std::function` callbacks.
- Declarative descriptors over builders: `CommandSpec`, `EffectDescriptor`,
  `Action`, menu context rows. Descriptors self-register via `Registry<T>` at
  their definition site (data-only at static init; no `Engine()` before Load).
- Consumer policy is injected once through `Engine().Policy` (PluginPolicy);
  kit code never hardcodes permission/immunity/reply behavior.
- Game thread only. The only threads are the database worker and HTTP's pool;
  both replay completions on the game thread via `Scheduler::EveryFrame` pumps.
- Public vocabulary is hoisted to `CS2Kit::Type` in `CS2Kit/Api.hpp`; prefer the
  short names over `CS2Kit::Module::Type`. In `.hpp` never use a namespace-scope
  using-directive; `using namespace CS2Kit::X;` is `.cpp`-only (TU-local).

## Design Notes

`CS2Kit::Core::PluginBase<TManagers>` is the recommended plugin entry point: it
owns the ISmmPlugin boilerplate, Load/Unload flow, standard SourceHook hooks,
the `PlayerManager` lifecycle, and constructs/destroys the plugin's `TManagers`
container (reached via the plugin's `App()`). All player-facing text goes
through `Engine().Messages` (MessageKind: Chat/Center/CenterHtml/Alert);
`PostgresDatabase` is async-first with blocking calls reserved for load time.
