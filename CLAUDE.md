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
gamedata/            Engine signatures and offsets
conan/               Canonical profiles and public remote configuration
recipes/             HL2SDK and Metamod Conan recipes
scripts/voltmod/     The `voltmod` Python CLI
templates/plugin/    Files copied by `voltmod new-plugin`
templates/project/   Files copied by `voltmod init`
test_package/        Conan package smoke test
tests/               SDK-free doctest suite, grouped by module
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

- `VoltMod::Runtime` contains Core, SDK, Players, Commands, Menu, HTTP, and App.
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

- `Sdk`, `Core`, `Http`, and `Database` never name `Runtime`. They take the
  sibling services they use. `modgraph` fails a `.cpp` in those modules that
  includes `VoltMod/Runtime.hpp`.
- `Players`, `Commands`, `Menu`, and `App` may take `Runtime&`, or the narrowest
  service that does the job.
- Header templates plugins instantiate (`Flow<TState>`, `PerSlot<T>`) take one
  service, so including them does not pull in the composition root.
- A file-static is only for engine callbacks that carry no user data (set and
  cleared by the service that owns it) or for process-wide sinks set once at
  load, such as the logger and the base directory.

Use these patterns throughout the framework:

- Plain-data descriptors such as `CommandSpec`, `Action`, and menu context rows
  are registered explicitly during load. Do not self-register at static init.
- Consumers inject permission, targeting, reply, and broadcast behavior once
  through `Runtime::Policy`.
- Registrations return a `[[nodiscard]] Subscription`. Store it beside the state
  its callback captures.
- Constructor injection is the default. Do not add process-lifetime singletons.
- Database and HTTP workers replay completions on the game thread through
  scheduler frame pumps.
- `<VoltMod/Api.hpp>` exports common short names into `VoltMod`. Database names
  remain in `<VoltMod/Database/Api.hpp>` so ordinary translation units do not
  include libpqxx.

## Module rules

`uv run poe modgraph` enforces the include allowlist in
`scripts/voltmod/modgraph.py`:

```text
Core      -> none
Http      -> Core
Sdk       -> Core
Players   -> Core, Sdk
Commands  -> Core, Sdk, Players
Menu      -> Core, Sdk, Players
Database  -> Core
App       -> all modules
```

An acyclic graph is not enough; an upward dependency still violates this
layering.

## Conventions

- Use C++23 and `.hpp` headers.
- Use `PascalCase` for types and methods and `_camelCase` for members.
- Use `std::format`, designated initializers, and `std::function` callbacks.
- Do not put namespace-scope using-directives in headers.
- Keep templates buildable and documentation examples aligned with the public
  headers.

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

Tests use doctest and must remain SDK-free unless a separate integration-test
surface is added. Each test case becomes a CTest entry; names must not contain
`[`, `]`, or `;`.
