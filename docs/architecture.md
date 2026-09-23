# Architecture {#architecture}

[TOC]

This is the page that explains why. Everything a plugin author does day to day is in
@ref plugin_guide and @ref host_guide.

## Modules

A module is a source directory and a layer in the include graph, not a namespace: every public
name lives directly in `VoltMod`, and moving a type between modules never renames it.

| Module | Holds |
| --- | --- |
| Core | Signals and subscriptions, results, load steps, text, per-slot state, timing, files, logging |
| Engine | Interfaces, gamedata bindings, `ConVar<T>`, clock, maps, precache, console commands |
| Schema | The generated field layout and its stamp |
| Entities | Entity lookup, the Entity/Pawn/Controller wrappers, schema fields, items |
| Events | The game event service and its typed event structs |
| Messaging | Chat and center-HTML messages, chat colors, the vote panel |
| Players | The roster, the Policy gate, action and effect dispatch |
| Hooks | Movement, visibility, teleport, chat input, client convars |
| Ui | Panorama `custom_hud_layout` panels and their button presses |
| Workshop | What connecting clients are told to download |
| Commands | The fluent builder, typed `Args`, the router |
| Menu | The menu model and `Flow`, drawn as center HTML |
| Http | Async HTTP and JSON REST helpers |
| Database | Async Postgres/MariaDB/SQLite and migrations |
| Unsafe | Opt-in raw hooking: `HookInterface`, `HookVirtual`, `HookFunction` |
| Host | The plain-data boundary between the host binary and a plugin |
| App | The composition root: `Runtime`, `Plugin`, `ServiceExchange` |

What each one may include:

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
Commands   -> Core, Engine, Entities, Messaging, Players, Host
Menu       -> Core, Engine, Entities, Messaging, Players, Hooks, Ui, Workshop
Http       -> Core
Database   -> Core
Unsafe     -> Core, Engine
Host       -> Core, Engine, Unsafe
App        -> every module
```

`voltmod modgraph` (`cli/voltmod/framework/layering.py`) enforces that block, rejects upward edges and
reports cycles. A module's own `Api.hpp` is exempt. Only `Commands` and `App` may name `Runtime`,
and header-only templates such as `Flow<TState>` and `PerSlot<T>` avoid the composition root so
consumer translation units stay narrow.

Where a lower module needs something the host resolved, it takes an injected callable instead of
an upward include: `Bindings::Bind` takes a `GameDataLookup`, and `App` adapts `IHostGameData` to
it in the `GameData` load step.

These are source layers, not link units. The framework ships `VoltMod::Sdk` and the optional
`VoltMod::Database`; `Host`'s implementation is compiled into the host binary only.

## The host and its plugins

```text
                         Metamod:Source
                               │  addons/metamod/voltmod.vdf
                               ▼
  ┌──────────────────────── voltmod host ────────────────────────┐
  │  8 engine hooks   gamedata   schema check   command names    │
  │  event fan-out    service table            `volt`            │
  └───┬──────────────────────────┬───────────────────────────┬───┘
      │ IHost view               │ IHost view                │
      ▼                          ▼                           ▼
 plugins/admin-system/      plugins/anticheat/            plugins/bhop/
   Plugin + Runtime          Plugin + Runtime             Plugin + Runtime
   its own allocator         its own allocator            its own allocator
```

One process, one host, one set of hooks. The host owns everything that can only exist once -
the engine hooks, the gamedata scan, the schema verification, the `volt` command, the table of
registered command names and the table of published interfaces - and hands each plugin a typed view
of it (`IHost`). A plugin is an ordinary library the host opens with `LoadLibrary` or `dlopen`.

Keeping the hooks in the host is what makes several plugins on one server cheap: they share one
frame hook and one chat hook instead of racing each other for Metamod slots, and a reload takes
down one plugin rather than the whole stack.

### Load sequence

1. Metamod loads `voltmod.vdf`, which is the host.
2. The host resolves `addons/voltmod/gamedata/gamedata.jsonc` once for the process. A signature a
   game update broke is logged here and nowhere else.
3. It verifies the live schema against the layout baked into this build, once, and remembers both
   the verdict and the layout stamp.
4. It installs the engine hooks and reserves the `volt` command name.
5. It reads every `addons/voltmod/plugins/*/plugin.json`, refuses the plugins whose required dependencies
   are not there with one `Refusing '<name>': <reason>` line each, and loads the rest alphabetically.
6. For each plugin it opens the library, resolves `VoltMod_PluginEntry`, checks the descriptor's
   ABI version and its `Load`/`Unload`/`Status` pointers, opens a host view under the plugin's
   name and log tag, then calls `Load`.
7. Inside the plugin, the internal module seeds its hook dispatch pointer, creates the `Runtime`,
   and runs `Runtime::Initialize`: logging, engine interfaces, then the framework load steps, among
   them the schema stamp comparison that refuses a plugin built against a different layout.
8. The module constructs the derived `Plugin`, subscribes to host events, then calls `Load`. A
   `false` from `Load` returns the first required step's reason to the host, which logs it as the
   refusal, destroys the plugin, and frees its library.
9. The host logs `N of M installed plugin(s) loaded.`

Unload runs in reverse: the plugin's commands, the plugin object, its host-event subscriptions,
then the `Runtime`. Only once nothing of the plugin is still running does the host
free the library - every hook thunk and subscription closure it installed is code inside it.

## Crossing the boundary

Each plugin carries its own copy of the static SDK and its own allocator, so the host/plugin
boundary is a real ABI boundary:

- Only plain data and borrowed views cross it. `include/VoltMod/Host/` holds plain structs and
  pure-virtual interfaces; nothing owning, such as `std::string`, appears in a signature. Text
  crosses as `std::string_view`, valid for the call only.
- Build plugins with the framework's toolchain: MSVC on Windows; GCC or Clang with libstdc++ on
  Linux. libc++ lays `std::string_view` out differently, and nothing at load time catches that.
- Nothing transfers ownership. Memory allocated on one side is freed on that side.
- No exception may unwind into the host. Every callback the host invokes is `noexcept` and logs
  what it caught.
- `PluginDescriptor::AbiVersion` must equal `HostAbiVersion`, and the plugin's schema layout stamp
  must equal the host's. Either mismatch refuses the plugin and says to rebuild it.

The same rules apply between two plugins, which is why @ref VoltMod::ServiceExchange interfaces
carry a version in their name; see @ref host_guide.

## Lifetimes

@ref VoltMod::Runtime is the service container for one load cycle: created on load, destroyed on
unload, with most services as direct members in dependency order. Your `App` holds what the plugin
owns for that same cycle and is destroyed first, so its subscriptions unregister while the
services they reference are still alive. That pair is what makes `volt reload` start clean.

```cpp
runtime.Players.Get(slot);
runtime.Messages.Reply(slot, "done");
runtime.Entities.PawnOf(slot).SetHealth(100);
runtime.World.Precache.Add("models/props/mine.vmdl");
```

Schema offsets are not a service. `voltmod schemagen` bakes them into the generated accessors at
build time, so reading `m_iHealth` needs nothing threaded through a constructor.

Three rules follow from the single-cycle model:

- **Game thread only.** Engine hooks and framework code run on the main thread. Database and HTTP
  workers queue completions and the `GameFrame` hook replays them through
  @ref VoltMod::Scheduler, so a callback never races game code.
- **Dependencies arrive through constructors.** The plugin constructor gets the runtime; every
  object below it gets only the services it uses. Nothing self-registers during static initialization.
- **Policy is injected once.** The framework has no admin model. A plugin fills `runtime.Policy`
  in `Load`, and one gate, `Policy::Authorize`, applies it to commands, targeting, actions,
  effects and menu rows. Anything declaring a permission is denied while `HasPermission` is
  unset. See @ref players_guide.

File-static state is reserved for engine callbacks that cannot carry user data and for
process-wide values such as the log handler and the base directory. The service that owns a
callback also sets and clears its static bridge.

## Signals and subscriptions

Fixed-signature signals are public @ref VoltMod::Event members subscribed with `+=`; game events
go through typed @ref VoltMod::GameEvents::On.

```cpp
_spawn = runtime.GameEvents.On<PlayerSpawn>([this](const PlayerSpawn& e) { OnSpawn(e.Slot); });
_slots = runtime.Slots.Changed += [this](int slot) { _state.Reset(slot); };
```

Both return a move-only, `[[nodiscard]]` @ref VoltMod::Subscription that unregisters on
destruction. `HookInterface` gives engine hooks the same lifetime, which is why cleanup is a
matter of member order rather than an unload routine.

An expensive event source takes an `EventLifecycle`: the first subscriber installs it, the last
removal uninstalls it, and if the hook cannot resolve, the subscription comes back empty with the
reason logged. Several events fed by one source share a `SharedLifecycle` that counts subscribers
across all of them, so the source is installed once.

Operations that can fail meaningfully return `Result<T>` or @ref VoltMod::Status, an
`std::expected` over @ref VoltMod::Error: a coarse `ErrorCode`, log text in `Detail`, and a
translation key in `Key` when a player is owed a reply.

Index a fixed-size, `MaxPlayers`-sized array only after `VoltMod::IsValidSlot`, and prefer
`PerSlot<T>`. An unchecked `[slot]` into a service the `Runtime` owns by value corrupts a
neighbouring member instead of failing.
