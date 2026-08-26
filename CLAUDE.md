# VoltMod repository

VoltMod is a C++23 framework for Counter-Strike 2 Metamod:Source plugins. This
directory is its own Git repository; when it is nested in `cs2-plugins`, inspect
and validate it separately from the parent worktree.

## Commands

```bash
uv sync
uv run poe doctor
uv run poe build
uv run poe build windows-msvc-release
uv run poe build-linux
uv run poe test
uv run poe lint
uv run poe format
uv run poe modgraph
uv run poe new-plugin <name>
voltmod init
```

`voltmod build` configures and compiles one preset; `voltmod test` brings that
build up to date and runs its CTest preset (`-R` filters cases). `voltmod
install [plugin]` merges a built plugin into a local CS2 server at
`CS2_SERVER_PATH`, `voltmod serve` runs that server, and `voltmod build
--install <plugin> --start` does all three. All CLI commands operate on the
current working directory, so run scaffolding commands from the consumer
repository.

## Repository map

```text
include/VoltMod/     Public C++ API by module
src/                 Framework implementation
cmake/               Plugin, test, library, and build-stamp helpers
gamedata/            gamedata.jsonc: where the engine keeps things, plus its schema
conan/               Canonical profiles and public remote configuration
recipes/             HL2SDK and Metamod Conan recipes
scripts/voltmod/     The `voltmod` Python CLI
templates/plugin/    Files copied by `voltmod new-plugin`
templates/project/   Files copied by `voltmod init`
test_package/        Conan package smoke test
tests/               HL2SDK-free doctest suite, grouped by module
docs/                Doxygen guides
```

HL2SDK and Metamod are Conan packages. The HL2SDK build module attaches the SDK
translation units that consumers must compile. There are no submodules.

## Package and build model

Consumers require the package, then call the CMake helpers delivered as Conan
build modules:

```cmake
find_package(voltmod CONFIG REQUIRED)
voltmod_add_plugin(my-plugin VERSION 1.0.0)
```

The framework builds two libraries:

- `VoltMod::Runtime` contains Core, Engine, Entities, Events, Messaging, Players,
  Hooks, Commands, Menu, HTTP, and App.
- `VoltMod::Database` contains the optional PostgreSQL layer.

`VoltMod::VoltMod` is the umbrella target. A plugin gets only Runtime by
default; `FEATURES DATABASE` adds Database. `voltmod_add_plugin` also configures
SDK glue, PCH, output layout, build metadata, VDF generation, and install
components.

The public presets are `windows-msvc-{release,debug}` and
`linux-steamrt-{release,debug}`. Treat their names as consumer API.

`voltmod package <build|publish|tag|prune|watch>` owns releases. Framework
versioning lives in `conanfile.py`; SDK revisions live in each recipe's
`conandata.yml`; remotes and ABI profiles live under `conan/`; tool versions live
in `pyproject.toml`.

## Runtime and API design

`VoltMod::MetamodPlugin` owns Metamod entry points, standard hooks, player
tracking, and one `VoltMod::Runtime` per load cycle. It passes the runtime to
`OnLoad(Runtime&, bool late)`. Consumers must destroy their own load-cycle state
in `OnUnload`.

The runtime is a flat service container. Services are accessed directly, such
as `runtime.Messages` and `runtime.Players`, so moving a service between source
modules does not rename the consumer API.

There is no ambient accessor for the runtime; everything is injected:

- `Core`, `Engine`, `Entities`, `Events`, `Messaging`, `Hooks`, `Http`, and
  `Database` never name `Runtime`. They take the sibling services they use.
  `modgraph` fails a `.cpp` in those modules that includes
  `VoltMod/Runtime.hpp`.
- `Players`, `Commands`, `Menu`, and `App` may take `Runtime&`, or the narrowest
  service that does the job.
- Header templates plugins instantiate (`Flow<TState>`, `PerSlot<T>`) take one
  service, so including them does not pull in the composition root.
- A file-static is only for engine callbacks that carry no user data (set and
  cleared by the service that owns it) or for process-wide sinks set once at
  load, such as the `Log::Sink` and the base directory.

Use these patterns throughout the framework:

- Plain-data descriptors such as `CommandSpec`, `Action`, and menu context rows
  are registered explicitly during load. Do not self-register at static init.
- Consumers inject permission, targeting, reply, and broadcast behavior once
  through `Runtime::Policy`.
- A fixed-signature signal is a public `Event<Args...>` member and `+=` is the only
  way to subscribe to one; `Raise` belongs to the owner. Game events go through
  `GameEvents::On<T>` and must have a struct in `Events/EventTypes.hpp` - there is
  no string form.
- An `Event` whose source costs something to run takes a `Lifecycle`: the first
  subscription installs it, the last one to drop removes it, and `OnFirst`
  returning false refuses the subscription after logging why. Services do not
  expose `Install()`/`Enable()` alongside it.
- Registrations return a `[[nodiscard]] Subscription`. Store it beside the state
  its handler captures. Dropping one unsubscribes, and a `Scheduler` one-shot is
  cancelled the same way.
- Fallible operations return `Result<T>`/`Status` over `Error`: `ErrorCode` to
  branch on, `Detail` for the log, `Key` for a player-facing reply.
- `gamedata/gamedata.jsonc` (version 2) says only *where* something is; C++ owns every
  prototype, vtable signature and field type in `Engine/Bindings.hpp`. A service takes
  `const Bindings&` and reads a typed field - never a string lookup on a call path. Parsing
  lives in `src/Engine/GameDataFile.*` and is SDK-free so it is unit-tested.
- Whether a feature works this load is `Runtime::Capabilities`, recorded once by
  `Runtime::Start` with the reason it is off. Services do not carry their own
  `Available()`/`IsResolved()`/`Installed()`/`Enabled()` flags, and one whose capability is off
  must be inert and safe to call.
- Enumerator names come from `Core/EnumNames.hpp` (`Name(value)`, `Parse<E>(text)`), not from
  hand-written switches.
- One convar is one `ConVar<T>` handle, resolved by name once. `SetMode` picks the write path:
  `Console` (a cfg line, replicates), `Server` (value only), `Raw` (storage poke, prefer
  `RawScope`).
- Constructor injection is the default. Do not add process-lifetime singletons.
- Database and HTTP workers replay completions on the game thread through
  scheduler frame pumps.
- `<VoltMod/Api.hpp>` gathers the public headers; every name it reaches is already
  spelled `VoltMod::Thing`. Database names stay in `<VoltMod/Database/Api.hpp>` so
  ordinary translation units do not include libpqxx.

## Module rules

`uv run poe modgraph` enforces the include allowlist in
`scripts/voltmod/modgraph.py`:

```text
Core       -> none
Engine     -> Core
Entities   -> Core, Engine
Events     -> Core, Engine, Entities
Messaging  -> Core, Engine, Entities, Events
Players    -> Core, Engine, Entities
Hooks      -> Core, Engine, Entities, Events, Players
Commands   -> Core, Engine, Entities, Players, Messaging
Menu       -> Core, Engine, Entities, Players, Messaging, Hooks
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
App        -> all modules
```

An acyclic graph is not enough; an upward dependency still violates this
layering.

## Conventions

- Use C++23 and `.hpp` headers.
- Use `PascalCase` for types and methods and `_camelCase` for members.
- Use `std::format`, designated initializers, and `std::function` callbacks.
- Every public name lives in one namespace, `VoltMod`. Modules are directories and
  layers, not namespaces. The only nested namespaces are small groups of free
  functions with a common noun (`VoltMod::Log`, `VoltMod::ChatColors`,
  `VoltMod::Validation`, `VoltMod::PawnOps`, `VoltMod::EffectOps`) and
  `VoltMod::Internal`, which may only appear under `src/`.
- Do not forward-declare a framework type. Include the header that defines it.
  `include/VoltMod/Engine/EngineTypes.hpp` is the one place a forward declaration
  belongs, and it says why for each name; a new one needs the same justification -
  an SDK type, a type defined under `src/`, or a pair that owns one another. A header
  declaring a name it goes on to define itself (a primary template before its partial
  specializations) is ordering its own contents, not standing in for an include.
- Do not use anonymous namespaces. A file-local helper is a `static` function or
  constant at the top of the .cpp, or a private static member when it needs class
  state.
- Do not use using-directives. A .cpp may name what it uses with targeted
  using-declarations (`using VoltMod::Player;`); a header may not.
- Keep templates buildable and documentation examples aligned with the public
  headers.

`uv run poe modgraph` enforces the last four alongside the layering.

## Commenting and documentation

- Comment only to explain contracts, ownership, lifetime, concurrency, module
  boundaries, build behavior, or compatibility. Keep comments near the code
  and remove stale or obvious narration.
- Use Doxygen for non-obvious public contracts and preserve exact symbols and
  tags. Keep `docs/` task-first, current, and explicit about commands, paths,
  and expected results.
- Keep examples and `templates/` synchronized with the public headers and
  generated output. Use plain English and sentence-case headings; call VoltMod
  the framework and reserve "library" for actual libraries or CMake targets.

Tests use doctest and must remain HL2SDK-free unless a separate integration-test
surface is added. Each test case becomes a CTest entry; names must not contain
`[`, `]`, or `;`.
