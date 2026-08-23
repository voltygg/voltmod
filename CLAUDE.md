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
src/                   Implementation, one directory per module (two libraries)
gamedata/              Engine signatures and offsets
cmake/                 CS2KitCommon.cmake (paths, platform, toolchain fallbacks)
                       + CS2KitPlugin.cmake (cs2_add_plugin, manifest/vdf, build stamping)
                       + CS2KitTests.cmake (cs2_add_tests) + DoctestMain.cpp + templates
                       + CS2KitLibrary.cmake (kit-internal: one static lib per target)
scripts/cs2kit/        Build + scaffolding tooling, shipped as the `cs2-kit` Python
                       distribution behind one `cs2kit` command (build, bootstrap,
                       format, modgraph, new-plugin, init); every subcommand
                       targets Path.cwd()
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

## Releasing

`cs2kit package <build|publish|tag|prune|watch>` is the whole release surface, and
CI is three workflows that call it: `ci.yml` (checks, then build the SDKs and the
kit against them), `publish.yml` (SDK packages on a `recipes/**` push to main,
cs2-kit on a `v*` tag), `watch.yml` (daily upstream check, weekly prune). Each
value has one home: `version.txt` for the kit's version, each recipe's
`conandata.yml` for its SDK pin, `conan/remotes.json` for the remote,
`conan/profiles/` for the ABI, `pyproject.toml` for tool versions.

## Build Commands

```bash
uv run poe build
uv run poe build windows-msvc-release
uv run poe build-linux
uv run poe new-plugin <name>   # scaffold a plugin into the invoking repo's plugins/
cs2kit init                    # stamp a whole consumer project (run from its root)
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
- One flat `CS2Kit::Runtime` per load cycle; no process-lifetime singletons.
  Objects take their collaborators through constructors, not from a global.
- Use `std::format`, designated initializers, and `std::function` callbacks.
- Declarative descriptors over builders: `CommandSpec`, `EffectDescriptor`,
  `Action`, menu context rows. They are plain data, registered explicitly from
  the consumer's load path - never self-registering at static init.
- Listener registrations return a `[[nodiscard]] Subscription` that unregisters
  on destruction; hold it next to whatever its callback captures.
- Consumer policy is injected once through `Runtime::Policy` (PluginPolicy);
  kit code never hardcodes permission/immunity/reply behavior.
- Game thread only. The only threads are the database worker and HTTP's pool;
  both replay completions on the game thread via `Scheduler::EveryFrame` pumps.
- Public vocabulary is hoisted to `CS2Kit::Type` in `CS2Kit/Api.hpp`; prefer the
  short names over `CS2Kit::Module::Type`. In `.hpp` never use a namespace-scope
  using-directive; `using namespace CS2Kit::X;` is `.cpp`-only (TU-local).

## Module Layering

`uv run poe modgraph` checks each module's includes against an explicit
allowlist in `scripts/cs2kit/modgraph.py`. A cycle check is not enough: an
upward edge stays acyclic and is exactly what breaks the layering.

```text
Core      -> (none)          primitives: ILogger, Paths, Slot, Scheduler, Subscription
Http      -> Core
Sdk       -> Core            everything engine-facing
Players   -> Core Sdk
Commands  -> Core Sdk Players
Menu      -> Core Sdk Players
Database  -> Core            option-gated on with_postgres
App       -> everything      the composition root: Runtime, MetamodPlugin
```

The build is two libraries, not one per module: `CS2Kit::Runtime` and
`CS2Kit::Database`. `cs2_add_plugin(<name> FEATURES DATABASE)` adds the second,
so a plugin with no database carries no libpqxx.

**Kit code reaches services through the references it was given.** The one
exemption is `CS2Kit::Detail::Rt()`, the ambient pointer to the live Runtime,
for the places that have no other channel: class templates instantiated in
consumer TUs (`Flow<TState>`, `PerSlot<T>`), ConVar and SourceHook trampolines,
and Metamod entry points. It is kit-internal; plugins never call it.

## Design Notes

`CS2Kit::App::MetamodPlugin` is the plugin entry point: it owns the ISmmPlugin
boilerplate, the Load/Unload flow, the standard SourceHook hooks and the
`PlayerManager` lifecycle, creates the `Runtime` for one load cycle and hands it
to `OnLoad(Runtime&, bool late)`. Whatever the plugin owns goes in a struct of
its own, built there and dropped in `OnUnload` - which is what makes a
`meta reload` start clean. All player-facing text goes through
`Runtime::Messages` (MessageKind: Chat/Center/CenterHtml/Alert);
`PostgresDatabase` is async-first with blocking calls reserved for load time.

`CS2KIT_PLUGIN(Klass)` expands the per-plugin entry-point ceremony (instance,
PLUGIN_EXPOSE); `PLUGIN_GLOBALVARS` ships inside MetamodPlugin.hpp.
`CS2Kit::LoadStandardConfig` is the standard OnLoad prelude (config +
translations as LoadReport stages). The default `OnPlayerChat` dispatches
registered commands, so a plugin with no chat customization writes none of that.
The Database vocabulary hoist lives in `<CS2Kit/Database/Api.hpp>`, deliberately
outside `Api.hpp`, so `<pqxx/pqxx>` only reaches TUs that opt in.
