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
  Hooks, Unsafe, Commands, Menu, HTTP, and App.
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
- `Commands` and `App` may take `Runtime&`, or the narrowest service that does the job.
  `Players` and `Menu` take the narrowest service directly - `ActionDispatcher(Policy&,
  PlayerManager&, EntitySystem&)`, `EffectDispatcher(ActionDispatcher&, EffectManager&)`,
  `MenuManager(Scheduler&, SlotEvents&, EntitySystem&, Messages&, ChatInput&, Translations&,
  Policy&, PlayerManager&)` - never `Runtime&`.
- Header templates plugins instantiate (`Flow<TState>`, `PerSlot<T>`) take one
  service, so including them does not pull in the composition root.
- A file-static is only for engine callbacks that carry no user data (set and
  cleared by the service that owns it), for process-wide sinks set once at load,
  such as the `Log::Sink` and the base directory, or for state that is genuinely
  process-wide rather than per-load: the schema field cache
  (`src/Entities/SchemaResolve.cpp`) is the one of those, because a class and
  field name resolve to the same offset for every plugin, Runtime and map in the
  process.

Use these patterns throughout the framework:

- Plain-data descriptors such as `Action` and menu context rows are registered
  explicitly during load, and a command is registered through the fluent
  `Commands.Add(name)...Run(handler)` builder whose handler signature is its
  argument spec. Do not self-register at static init.
- Consumers inject permission, targeting, reply, and broadcast behavior once
  through `Runtime::Policy`, and `Policy::Authorize(caller, target, permission)`
  is the only place the framework applies it. Commands, actions, effects and menu
  rows call that one gate; nothing repeats its steps, and denial is a
  `Result<Authorized>` error rather than a nulled-out field.
- `PlayerRef` is what a slot is stored as, `Player&` is who is connected now, and
  `Controller`/`Pawn` are this frame's entities. `PlayerManager` owns the roster
  and raises `Connected`, `FullyConnected`, `SettingsChanged` and `Disconnected`;
  there are no player-lifecycle virtuals on `MetamodPlugin`.
- A fixed-signature signal is a public `Event<Args...>` member and `+=` is the only
  way to subscribe to one; `Raise` belongs to the owner. Game events go through
  `GameEvents::On<T>` and must have a struct in `Events/EventTypes.hpp` - there is
  no string form.
- An `Event` whose source costs something to run takes a `Lifecycle`: the first
  subscription installs it, the last one to drop removes it, and `OnFirst`
  returning false refuses the subscription after logging why. Services do not
  expose `Install()`/`Enable()` alongside it.
- A vtable hook is a `VOLTMOD_VHOOK*` declaration at file scope plus a
  `VtableHook` member (`<VoltMod/Unsafe/VtableHook.hpp>`): the declaration is one
  hooked vfunc per translation unit - `SH_MANUALHOOK_RECONFIGURE` mutates the
  file-static it creates - and the member owns the pre/post pair, installs
  pair-or-nothing, and removes by id when it is dropped. No service repeats the
  reconfigure/add/id bookkeeping, and no SourceHook `SH_*` add or remove macro
  appears outside that one macro.
- Registrations return a `[[nodiscard]] Subscription`. Store it beside the state
  its handler captures. Dropping one unsubscribes, and a `Scheduler` one-shot is
  cancelled the same way.
- Fallible operations return `Result<T>`/`Status` over `Error`: `ErrorCode` to
  branch on, `Detail` for the log, `Key` for a player-facing reply.
- `Entity`, `Pawn` and `Controller` are frame-local wrappers, not handles to keep.
  `explicit operator bool()` is the only validity check, they copy but do not
  assign, and anything stored is an `EntityRef` or a `PlayerRef` re-resolved
  through `EntitySystem`. A schema field is a `Field<T, "Class", "m_name">` member
  that resolves its own offset once per process, walks base classes, and dirties a
  networked write - there is no schema service to inject, and no string-pair
  lookup on a call path.
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
- One convar is one `ConVar<T>` handle, resolved by name once. `Set` uses a cfg line so replicated
  values reach clients; `RawScope` temporarily pokes storage without callbacks or networking.
- Constructor injection is the default. Do not add process-lifetime singletons.
- Database and HTTP workers replay completions on the game thread through
  scheduler frame pumps.
- `<VoltMod/Api.hpp>` gathers the core vocabulary, `Runtime`, players, commands, and
  plugin plumbing; every name it reaches is already spelled `VoltMod::Thing`. It never
  reaches nlohmann or the Menu-building surface - `<VoltMod/Entities/Api.hpp>`,
  `<VoltMod/Hooks/Api.hpp>`, `<VoltMod/Menu/Api.hpp>` and `<VoltMod/Unsafe/Api.hpp>` gather
  the rest of those modules' public surfaces, and `<VoltMod/App/Config.hpp>` gathers
  `JsonConfig`, `StandardPluginSettings` and `Json` for a plugin's own `Config.hpp`.
  Database names stay in `<VoltMod/Database/Api.hpp>` so ordinary translation units do
  not include libpqxx.

## Module rules

`uv run poe modgraph` enforces the include allowlist in
`scripts/voltmod/modgraph.py`:

```text
Core       -> nothing
Engine     -> Core
Entities   -> Core, Engine
Events     -> Core, Engine, Entities
Messaging  -> Core, Engine, Entities, Events
Players    -> Core, Engine, Entities
Hooks      -> Core, Engine, Entities, Events, Players, Unsafe
Commands   -> Core, Engine, Entities, Messaging, Players
Menu       -> Core, Engine, Entities, Messaging, Players, Hooks
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
App        -> every module
```

An acyclic graph is not enough; an upward dependency still violates this
layering. A module's own `Api.hpp` (`Entities/Api.hpp`, `Hooks/Api.hpp`, ...) is exempt: it
is a deliberate cross-module aggregate documenting that module's public surface - `Hooks/Api.hpp`
gathering `Events` and `Messaging` types is not the `Hooks` module depending on them. `modgraph`
also rejects `#include <nlohmann/...>` anywhere under `include/VoltMod` or `src` except
`Core/Json.hpp`, `App/Config.hpp`, `App/JsonConfig.hpp`, `App/PluginSettings.hpp`,
`Engine/GameDataFile.*`: route JSON use through `<VoltMod/Core/Json.hpp>`
instead, and Core, Engine, Entities, Events, Messaging, Players, Hooks and Commands may not
include `Menu/` or `App/` at all - both would leak into every consumer of that layer.

## Conventions

- Use C++23 and `.hpp` headers.
- Use `PascalCase` for types and methods and `_camelCase` for members.
- Use `std::format`, designated initializers, and `std::function` callbacks.
- Every public name lives in one namespace, `VoltMod`. Modules are directories and
  layers, not namespaces. The only nested namespaces are small groups of free
  functions with a common noun (`VoltMod::Log`, `VoltMod::ChatColors`,
  `VoltMod::Validation`, `VoltMod::PawnOps`), `VoltMod::Args` - the command
  argument types, whose names (`Target`, `Int`, `Word`, `Rest`) are too generic
  to carry at `VoltMod::` scope and appear nowhere but a handler's parameter
  list - and `VoltMod::Internal`, which may only appear under `src/`.
- Do not forward-declare a framework type in a header. Include the header that
  defines it. `include/VoltMod/Engine/EngineTypes.hpp` is the one place a forward
  declaration belongs - modgraph names that path, so a new home is a tooling
  change - and it says why for each name; a new one needs the same justification -
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
