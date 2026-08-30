# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

## What gamedata is for

`Runtime::Start()` loads `gamedata/gamedata.jsonc` before plugin `OnLoad`.
Gamedata records where engine members are; @ref VoltMod::Bindings defines their
C++ types.

```cpp
// include/VoltMod/Engine/Bindings.hpp
Fn<CEntityInstance*(const char*, int)> CreateEntityByName;   // signatures.CreateEntityByName
VFn<void(int)> ChangeTeam;                                   // vtables.ChangeTeam
OffsetOf<int> ServerSideClientSlot;                          // offsets.ServerSideClientSlot
```

Services use `const Bindings&`, avoiding string lookup on runtime call paths.
Binding failures are reported during load.

## `gamedata.jsonc` format (version 2)

Four sections, each keyed by the name `Bindings` uses. A key may appear in only one of them.

```jsonc
{
  "$schema": "./gamedata.schema.json",
  "version": 2,
  "build": { "game": "cs2", "verified": "2026-08-26", "note": "re-verify after every CS2 engine update" },

  // A byte pattern scanned in one loaded module. The match address is the binding.
  "signatures": {
    "CreateEntityByName": {
      "library": "server",                                  // default; "engine2" for engine code
      "windows": { "pattern": "48 83 EC 48 C6 44 24 30 00" },
      "linux":   { "pattern": "48 8D 05 ? ? ? ? 55 48 89 FA" }
    }
  },

  // A pointer reached through a rel32 displacement inside a matched signature.
  "addresses": {
    "GameEventManager": {
      "signature": "GameEventManagerSig",                    // must exist in "signatures"
      "rel32At": { "windows": 98, "linux": 106 }             // bytes from the match to the rel32
    }
  },

  // A vtable slot, plus the class whose table the index is counted in.
  "vtables": {
    "RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "ProcessRespondCvarValue": { "class": "CServerSideClient", "library": "engine2", "windows": 38, "linux": 40 }
  },

  // A byte offset into a layout the SDK headers do not declare.
  "offsets": {
    "CheckTransmitPlayerSlot": { "windows": 576, "linux": 576, "max": 4096, "align": 1 }
  }
}
```

Wildcard bytes are `?` or `??`. Optional `max` and `align` constraints reject invalid offsets;
their defaults are `4096` and `1`.

`gamedata.schema.json` sits next to the file with `additionalProperties: false` everywhere, so an
editor squiggles a typo before the server ever sees it.

## What the loader checks

The parser rejects the file before scanning when it has:

- no `version`, or a `version` this build does not read;
- one key in two sections;
- an entry with no column for the platform being loaded;
- a malformed byte pattern;
- a negative `rel32At`, or an `addresses` entry naming a signature that does not exist;
- a vtable index outside `[0, 500)`;
- an offset above its `max`, or not a multiple of its `align`.

Resolution failures do not reject the file. `GameData::FailureSummary()` reports them, and affected
capabilities carry the same reason.

`FailureSummary()` cannot include a missing key because no entry was loaded.
`Bindings::Bind` reports it through its capability or a warning that names the
missing key.

## Capabilities, not readiness flags

`Runtime::Start` records availability in @ref VoltMod::Capabilities.

```cpp
if (!runtime.Capabilities.Has(Capability::Movement))
    Log::Warn("no movement feed: {}", runtime.Capabilities.Reason(Capability::Movement));
```

Load logs and the `capabilities` status section report the same summary. Unavailable services remain
safe to call and return `Error::NotReady`, an empty `Subscription`, or no result.

## Re-verify after an engine update

Every entry can drift after a CS2 update. Treat an older `build.verified` date as
unverified.

1. **Read the load and capability reports.** Listed entries are broken; unlisted entries still need
   verification.
2. **Re-sync patterns** with the upstream named in `gamedata.jsonc`. Lengthen ambiguous patterns.
3. **Re-check every vtable index.** Executable-section validation catches invalid slots, but not a
   valid slot that points to the wrong function.
4. **Re-check every byte offset.** Stale offsets can read plausible unrelated data. Keep `max` and
   `align` tight.
5. **Exercise each feature on a live server.** Successful resolution does not prove correct behavior.
6. **Update `build.verified`** in the same change.

The entries most likely to bite, and how each one fails:

| Entry | Section | Used by | Drift symptom |
| --- | --- | --- | --- |
| `RunCommand` | vtables | @ref VoltMod::Movement | Crash on the first movement tick, unless the executable-section check catches it first |
| `Teleport` | vtables | @ref VoltMod::Teleport | Missing: subscribing to `Teleported` is refused and `Capability::Teleport` is off |
| `ProcessRespondCvarValue` | vtables | @ref VoltMod::ClientCvars | `Capability::ClientCvars` off; client convar queries unavailable |
| `UserCmdPB` | offsets | `Movement` cmd events | Missing: `Valid=false` views. Stale: garbage viewangles and buttons |
| `UserCmdNumber` | offsets | `UserCmdView::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which live clients leave at 0. Stale: a counter that never increments by 1 |
| `ServerSideClientSlot` | offsets | `ClientCvars` | Stale: a client's answer is attributed to the wrong player |
| `CheckTransmitPlayerSlot` | offsets | @ref VoltMod::Transmit | Stale: the wrong recipient is filtered |
| `GameEventManager` | addresses | @ref VoltMod::Messages | Center HTML does not display |

## Signature scanning

The internal scanner rejects ambiguous patterns and out-of-bounds rel32 targets.
Plugins use it through gamedata rather than directly.

### Vtable lookup by class name

`FindVirtualTable(moduleName, className)` resolves primary class tables. `VHookBinding` keeps each
resolved table with its slot.

- Windows: walks the module's RTTI: the type descriptor for `.?AV<class>@@`, the complete object
  locator referencing it, then the vtable that follows. Only a locator at offset 0 is accepted, so
  the result is always the class's primary vtable, never a base subobject's. The name has to be the
  top-level class name exactly: a `struct` (`.?AU`), a nested class, or a namespaced one resolves to
  null.
- Linux: reads `_ZTV<mangled>` from the ELF `.symtab`, falling back to `.dynsym`. A fully stripped
  module has neither.

Lookup returns null on failure. The gamedata entry still supplies the slot.

## Schema fields

Schema offsets need no gamedata: `voltmod schemagen` bakes them into the generated accessors,
and `Runtime::Start` verifies the whole layout against the live schema once at load:

```cpp
runtime.Entities.PawnOf(slot).SetHealth(100);   // writes CBaseEntity::m_iHealth at a baked offset
```

Gamedata and the schema answer different questions, and both are baked rather than searched at
each call:

| | says where | source | checked |
| --- | --- | --- | --- |
| `gamedata/gamedata.jsonc` | functions, vtables, interfaces | hand-maintained | at load, per entry |
| `schema/server.json` | entity field offsets | dumped from the engine | at load, whole layout |

Schema fields are generated accessors, so a sub-object is a hop rather than a follow:

```cpp
int money = controller.InGameMoneyServices().Account();
```

Every accessor answers harmlessly on a falsy view, and a `Set` dirties the field for replication
on its own. Use them only on the game thread.
