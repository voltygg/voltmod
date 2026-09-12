# VoltMod

C++23 framework for Counter-Strike 2 Metamod:Source plugins. This is its own Git
repo; when nested in `cs2-plugins`, inspect and validate it separately.

## Comments and names

MUST:

- Comment only where the code cannot say it: intent, ownership, lifetime, threading, compatibility.
- One line inside a function. A public contract may take a few lines of Doxygen. No essays.
- Names are plain words a new developer understands. If a name needs a comment to decode it, rename it.

## Commands

```bash
uv run poe doctor
uv run poe build [preset]      # windows-msvc-{release,debug}, linux-steamrt-{release,debug}
uv run poe test                # build, then CTest (-R filters)
uv run poe lint | format | modgraph
uv run poe build --install <plugin> --start   # install to CS2_SERVER_PATH and launch
uv run poe panorama            # compile panorama/ UI into the client (Windows)
voltmod panorama render | check | preview | publish   # screens: docs/panorama.md
voltmod gamedata check | resolve --write      # after a CS2 update: docs/sdk/gamedata.md
voltmod schemagen                             # regenerate accessors from the server's own dump
voltmod init | new-plugin <name>              # run from the consumer repo
voltmod package <build|publish|tag|prune|watch>
```

Preset names are consumer API.

## Layout

```text
include/VoltMod/  Public API by module     src/        Implementation
cmake/            Plugin and test helpers  gamedata/   gamedata.jsonc + schema
conan/            Profiles and remote      recipes/    HL2SDK and Metamod recipes
cli/voltmod/      Python CLI (tests: cli/tests/)   templates/  new-plugin and init files
tests/            SDK-free doctest suite   docs/       Doxygen guides
```

Consumers call `find_package(voltmod CONFIG REQUIRED)` and `voltmod_add_plugin(name VERSION v)`
(`FEATURES DATABASE` for PostgreSQL, MariaDB or SQLite). Targets: `VoltMod::Runtime`, `VoltMod::Database`,
`VoltMod::Headers` (SDK-free, for tests). Versions: framework in `conanfile.py`, SDK in each
recipe's `conandata.yml`, tools in `pyproject.toml`.

## Module layering

`uv run poe modgraph` enforces what each module may include:

```text
Core       -> nothing
Engine     -> Core
Schema     -> Core, Engine
Entities   -> Core, Engine, Schema
Events     -> Core, Engine, Entities
Messaging  -> Core, Engine, Entities, Events
Players    -> Core, Engine, Entities
Hooks      -> Core, Engine, Schema, Entities, Events, Players, Unsafe
Ui         -> Core, Engine, Schema, Entities, Hooks, Unsafe
Workshop   -> Core, Engine, Players, Unsafe
Commands   -> Core, Engine, Entities, Messaging, Players
Menu       -> Core, Engine, Entities, Messaging, Players, Hooks, Ui, Workshop
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
App        -> every module
```

A module's own `Api.hpp` is exempt (deliberate aggregate). Nothing below Menu includes
`Menu/` or `App/`. Only `Commands` and `App` may name `Runtime`.

Design rules and conventions are in `.claude/rules/` and load per file path.
