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
include/CS2Kit/        Public API headers, one directory per module
src/                   Implementation, one static library per module
gamedata/              Engine signatures and offsets
cmake/                 CS2KitCommon.cmake (paths, platform, toolchain fallbacks)
                       + CS2KitPlugin.cmake (cs2_add_plugin, manifest/vdf, build stamping)
                       + CS2KitTests.cmake (cs2_add_tests) + DoctestMain.cpp + templates
                       + CS2KitLibrary.cmake (kit-internal: one static lib per target)
scripts/cs2kit/        Build + scaffolding tooling, shipped as the `cs2-kit` Python
                       distribution (cs2kit-build, -bootstrap, -format, -new-plugin,
                       -init-project); every entry point targets Path.cwd()
templates/plugin/      Plugin scaffold tree ($name/$ns/... placeholders)
templates/project/     Consumer-project scaffold
test_package/          conan create validation: hello plugin via cs2_add_plugin
tests/                 SDK-free unit tests (doctest + ctest); see docs/testing.md
docs/                  Doxygen pages and guides
CMakeLists.txt         Standalone CMake build
CMakePresets.json      Windows/Linux presets
conanfile.py           This repo's consumer conanfile AND its package recipe
conan/                 profiles/ + remotes.json, installed together with
                       `conan config install <repo> -sf conan`
```

There are no submodules. hl2sdk-cs2 and metamod-source are Conan packages built
from the recipes in `recipes/`; hl2sdk's build module attaches the SDK sources a
consumer compiles (`hl2sdk_attach_*`). Preset names are public API for consumers -
rename with care.

## Build Commands

```bash
uv run poe build
uv run poe build windows-msvc-release
uv run poe build-linux
uv run poe new-plugin <name>   # scaffold a plugin into the invoking repo's plugins/
cs2kit-init-project            # stamp a whole consumer project (run from its root)
```

`poe build` runs the full workflow preset (configure, build, ctest). Consuming
repos install this distribution and get the same tasks; there are no wrapper
scripts anywhere.

Consuming projects find the package and declare plugins with the kit-provided
`cs2_add_plugin`, which reaches them as a CMakeDeps build module:

```cmake
find_package(cs2-kit CONFIG REQUIRED)
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

## Module Layering

The modules form a DAG, and `uv run poe modgraph` fails the build if that stops
being true:

```text
Core      -> (none)          primitives: ILogger, Paths, Registry, Slot, Scheduler
Utils     -> Core
Http      -> Utils
Sdk       -> Core Utils      everything engine-facing
Players   -> Core Sdk Utils
Commands  -> Core Players Sdk Utils
Menu      -> Core Players Sdk Utils
Database  -> Core Utils      option-gated on with_postgres
App       -> everything      the composition root: Services, PluginBase, Engine()
```

Each layer builds its own static library and is a Conan component, so
`cs2_add_plugin(<name> COMPONENTS App)` links App and its dependencies but not
Database - a plugin with no database carries no libpqxx. Omitting COMPONENTS
links `CS2Kit::CS2Kit`, which is everything.

**Only App may use `Engine()`.** Inside a module, reach the layer you need
through its own accessor - `Core::Ctx()`, `Utils::Ctx()`, `Sdk::Ctx()`,
`Players::Roster()`, `Commands::Manager()`, `Menu::Menus()` - and downward only.
Plugins are consumers, not modules, and use `Engine()` as before.

## Design Notes

`CS2Kit::Core::PluginBase<TManagers>` is the recommended plugin entry point: it
owns the ISmmPlugin boilerplate, Load/Unload flow, standard SourceHook hooks,
the `PlayerManager` lifecycle, and constructs/destroys the plugin's `TManagers`
container (reached via the plugin's `App()`). All player-facing text goes
through `Engine().Messages` (MessageKind: Chat/Center/CenterHtml/Alert);
`PostgresDatabase` is async-first with blocking calls reserved for load time.

`CS2KIT_PLUGIN(Klass, Ns)` expands the per-plugin entry-point ceremony
(instance, PLUGIN_EXPOSE, App() trampoline); `PLUGIN_GLOBALVARS` ships inside
MetamodPluginBase.hpp. `CS2Kit::LoadStandardConfig` is the standard OnLoad
prelude (config + translations as LoadReport stages); self-registered
`CommandSpec`s are auto-ingested after OnLoad and the default `OnPlayerChat`
dispatches them, so a plugin with no chat customization writes none of that.
The Database vocabulary hoist lives in `<CS2Kit/Database/Api.hpp>`, deliberately
outside `Api.hpp`, so `<pqxx/pqxx>` only reaches TUs that opt in.
