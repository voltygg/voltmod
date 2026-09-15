# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

## What gamedata is for

`Runtime::Start()` loads `gamedata/gamedata.jsonc` before plugin `OnLoad`.
Gamedata records engine locations; @ref VoltMod::Bindings defines their C++
types.

```cpp
// include/VoltMod/Engine/GameData/Bindings.hpp
Fn<CEntityInstance*(const char*, int)> CreateEntityByName;   // functions."CreateEntityByName"
Address GameEventManager;                                   // globals."CSource2Server::g_GameEventManager"
VirtualFn<void(CEntityInstance*, int)> ChangeTeam;          // vtables."CCSPlayerController::ChangeTeam"
OffsetOf<int> ClientSlot;                                   // offsets."CServerSideClientBase::m_nClientSlot"
```

Services use `const Bindings&`, avoiding string lookup on runtime call paths.
`Bindings::Load` reads the file and binds every member in one pass.

## `gamedata.jsonc` format

Four sections, named after what they bind. Each key is the engine's own name for the symbol,
`Class::member` where it has one, so it can be looked up in upstream gamedata and in the binary;
`Bindings::Load` maps it to a plain C++ name. A key belongs to one section.

```jsonc
{
  "$schema": "./gamedata.schema.json",
  // The steam.inf ServerVersion the entries were last checked on, and the date.
  "build": { "server": "2000908", "verified": "2026-09-11" },

  // A byte pattern matching the start of a function. The match address is the binding.
  "functions": {
    "CreateEntityByName": {
      "module": "server",                                  // default; "engine2" for engine code
      "windows": "48 83 EC 48 C6 44 24 30 00",
      "linux": "48 8D 05 ? ? ? ? 55 48 89 FA"
    }
  },

  // A global reached through the rel32 displacement `rel32At` bytes after a pattern's match.
  "globals": {
    "CBaseGameSystemFactory::sm_pFirst": {
      "windows": { "pattern": "48 8B 1D ? ? ? ? 48 85 DB 0F 84 ? ? ? ? BD", "rel32At": 3 },
      "linux": { "pattern": "4C 8B 35 ? ? ? ? 4D 85 F6 75 ? E9", "rel32At": 3 }
    }
  },

  // A vtable slot, counted in the primary table of `class`, or in the table of its base `base`.
  "vtables": {
    "CPlayer_MovementServices::RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "CServerSideClient::ProcessRespondCvarValue": { "class": "CServerSideClient", "module": "engine2", "windows": 38, "linux": 40 }
  },

  // A byte offset into a layout the SDK headers do not declare, or where a base sits in a class.
  "offsets": {
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576 },
    "CServerSideClient::INetworkMessageProcessingPreFilter": {
      "class": "CServerSideClient", "base": "INetworkMessageProcessingPreFilter", "module": "engine2"
    }
  }
}
```

A base offset and a `base` vtable are read from RTTI at load, so they need no per-platform number
and cannot go stale. A base that appears more than once in the class, or a virtual base, is refused.

Wildcard bytes are `?` or `??`. `gamedata.schema.json` sits next to the file with
`additionalProperties: false` everywhere, so an editor flags a typo before the server sees it.

## What the loader checks

The file is read strictly. An unknown key, a value of the wrong type, or a section that is not an
object refuses the whole file, and nothing binds.

Then each member binds from its key, or fails with a line naming the key and the reason:

- the key is missing, is in two sections, or is in a section that member does not bind from;
- the entry has no column for this platform (an entry nothing binds may leave it out);
- a pattern is empty, its module is not loaded, or it matches nowhere or more than once;
- a global's rel32 displacement, or the address it points at, is outside its module or unreadable;
- an offset or index is negative;
- a vtable's class table is not found, or its slot does not hold code;
- a `base` is not in its class through RTTI, is in it more than once, is virtual, or has no vtable
  of its own where a slot is counted in it.

The `GameData` load step lists every failure, and each feature reports its own from
`Available()`. An entry nothing binds is a warning, not a failure.

A pattern proves itself by matching once; a vtable index or an offset cannot. When `build.server`
is not the running server's version, the load warns that the file's vtable indices and offsets are
unchecked on this build.

The log records where each vtable slot's code lives as `key=module+offset`, to match a crash dump
against a binding.

### The resolved record

After a load where every member bound, the framework writes what resolved to
`addons/voltmod/gamedata/resolved.<platform>.json`: the server build, module-relative addresses for
functions, globals and class tables, and each slot index and offset. It is written once per server
build, so the next plugin to load on that build leaves it alone, and a failed write never fails a
load. Keep the record from a build that worked to compare against after an update.

## Availability

Each optional feature says whether it works this load, and why not:

```cpp
if (auto available = runtime.Hooks.Movement.Available(); !available)
    Log::Warn("no movement feed: {}", available.error().Detail);
```

The load log names every unavailable feature once, and the `load` status section lists the same.
A service that is not available stays safe to call and returns an error, an empty `Subscription`,
or no result.

## Re-verify after an engine update

Every entry can drift after a CS2 update. Treat a `build.server` that is not the running server's
as unverified.

1. **Run `voltmod gamedata check`.** It reports, against the installed binaries and in a second,
   which `functions` and `globals` patterns no longer match. Prefer it to the load log: it needs
   no server, and it says why an entry drifted rather than only that it did.
2. **Run `voltmod gamedata resolve --write`** to repair what it can, then read the diff. It only
   ever widens a struct displacement that moved, and only when exactly one such change brings the
   pattern back to a single match. Anything else it refuses, and those you re-sync by hand from
   the upstream named in `gamedata.jsonc`.
3. **Re-check every vtable index.** The slot check catches an index that lands on data, but not a
   valid slot holding the wrong function.
4. **Re-check every byte offset.** Stale offsets can read plausible unrelated data.
5. **Exercise each feature on a live server.** Successful resolution does not prove correct behavior.
6. **Update `build.server` and `build.verified`** in the same change.

The entries most likely to bite, and how each one fails:

| Entry | Section | Used by | Drift symptom |
| --- | --- | --- | --- |
| `CPlayer_MovementServices::RunCommand` | vtables | @ref VoltMod::Movement | Crash on the first movement tick, unless the slot check catches it |
| `CBaseEntity::Teleport` | vtables | @ref VoltMod::Teleport | Missing: subscribing to `Teleported` is refused and `Teleport::Available` says why |
| `CServerSideClient::ProcessRespondCvarValue` | vtables | @ref VoltMod::ClientConVars | `ClientConVars::Available` fails; client convar queries unavailable |
| `CUserCmd::CSGOUserCmdPB` | offsets | `Movement` cmd events | Missing: `Valid=false` views. Stale: garbage viewangles and buttons |
| `GameEntitySystem` | offsets | @ref VoltMod::EntitySystem | Stale: the pointer read is not a `CGameEntitySystem`, so entity lookups return nothing and the load log says so |
| `CUserCmdBase::cmdNum` | offsets | `PlayerInput::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which live clients leave at 0. Stale: a counter that never increments by 1 |
| `CServerSideClientBase::m_nClientSlot` | offsets | `ClientConVars`, `ButtonPresses` | Stale: a client's answer is attributed to the wrong player |
| `INetworkMessageProcessingPreFilter::FilterMessage` | vtables (base) | @ref VoltMod::ScreenManager::Pressed | Missing: `ScreenManager::Available` fails; presses never arrive. Stale index: a different filter function is hooked |
| `CServerSideClient::INetworkMessageProcessingPreFilter` | offsets (RTTI) | @ref VoltMod::ScreenManager::Pressed | Missing: the base was renamed or its RTTI changed; `ScreenManager::Available` fails |
| `CNetworkGameServer::ReplyConnection` | functions | @ref VoltMod::Addons | Missing: `Require` is refused with the reason |
| `CNetworkGameServer::m_szAddons` | offsets | @ref VoltMod::Addons | Stale: clients download addons but mount none, or a corrupted reply |
| `CheckTransmitPlayerSlot` | offsets | @ref VoltMod::Visibility | Stale: the wrong recipient is filtered |
| `CSource2Server::g_GameEventManager` | globals | @ref VoltMod::GameEvents | Center HTML does not display |

## Checking and repairing offline

`voltmod gamedata check` and `voltmod gamedata resolve` read the shipped binaries and never start
the game. `--game-dir` defaults to `CS2_SERVER_PATH`, and the platform comes from whichever
binaries that directory holds, so the Linux column is checked by pointing them at a folder
holding `libserver.so` and `libengine2.so` copied off a server.

What `resolve --write` will do, and what it will not:

- An entry that still matches once is never rewritten. The file only changes where it was wrong.
- An entry that misses is repaired only by widening one struct displacement, the bytes a pattern
  should never have pinned, and only when exactly one such change brings it back to a single
  match. Two viable candidates means nothing can say which drifted, so it refuses.
- It never searches for a function. Every binding is carried forward from one a human verified,
  which is why the last step of the procedure above does not go away.

```text
==> gamedata windows (game build 2000908)
    21/22 patterns hold
    REPAIRED  functions.CCSCustomHudLayout::SetInputCaptureEnabled
              bytes 12-15: 1200 -> 1208, wildcarded
              1208 is CCSCustomHudLayout::m_vecPlayerLayoutStates
```

## Pattern scanning

The internal scanner rejects ambiguous patterns, and a global's rel32 target must land inside its
module. Plugins use it through gamedata rather than directly.

### Vtable lookup by class name

`FindVirtualTable(moduleName, className)` resolves primary class tables, and `FindBaseIn` finds
where a base sits in a class and that base's own table. `VirtualFn` keeps each resolved table with
its slot.

- Windows: walks the module's RTTI: the type descriptor for `.?AV<class>@@` or `.?AU<class>@@`,
  the complete object locator referencing it (its signature and its own RVA must match), then the
  vtable that follows. A base's offset comes from the class hierarchy's base list, and its table
  follows the locator at that offset. The name has to be the top-level class name exactly: a nested
  or namespaced class resolves to null.
- Linux: reads `_ZTV<mangled>` from the ELF `.symtab`, falling back to `.dynsym`. The game's
  modules hide those symbols, so it then walks the Itanium RTTI in the mapped module. A base's
  offset is summed along the typeinfo base lists, and its table is the one whose offset-to-top is
  minus that offset.

When lookup fails, the entry does not bind.

## Schema fields

Schema offsets need no gamedata: `voltmod schemagen` bakes them into the generated accessors,
and `Runtime::Start` verifies the whole layout against the live schema once at load. The input
`schemagen` needs, `addons/voltmod/schema/server.json`, also records which fields the engine sends
to clients. That comes from the engine's network serializers, which exist only while a map runs, so
a plugin writes the dump at map start, or at load when it loads into a running map, and skips it
when the dump on disk already carries the running game build. A failed write never fails a load.

```cpp
runtime.Entities.PawnOf(slot).SetHealth(100);   // writes CBaseEntity::m_iHealth at a baked offset
```

Gamedata and the schema answer different questions, and both are baked rather than searched at
each call:

| | says where | source | checked |
| --- | --- | --- | --- |
| `gamedata/gamedata.jsonc` | functions, globals, vtable slots, offsets | hand-maintained | at load, per entry |
| `schema/server.<platform>.json` | entity field offsets | dumped from the engine | at load, whole layout |

Schema fields are generated accessors, so a sub-object is a hop rather than a follow:

```cpp
int money = controller.InGameMoneyServices().Account();
```

Every accessor answers harmlessly on a falsy view, and a `Set` dirties the field for replication
on its own. Use them only on the game thread.
