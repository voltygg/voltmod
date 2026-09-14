# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

## What gamedata is for

`Runtime::Start()` loads `gamedata/gamedata.jsonc` before plugin `OnLoad`.
Gamedata records engine locations; @ref VoltMod::Bindings defines their C++
types.

```cpp
// include/VoltMod/Engine/GameData/Bindings.hpp
Fn<CEntityInstance*(const char*, int)> CreateEntityByName;   // signatures."CreateEntityByName"
VirtualFn<void(int)> ChangeTeam;                            // vtables."CCSPlayerController::ChangeTeam"
OffsetOf<int> ClientSlot;                                   // offsets."CServerSideClientBase::m_nClientSlot"
```

Services use `const Bindings&`, avoiding string lookup on runtime call paths.
Binding failures are reported during load.

## `gamedata.jsonc` format

Four sections. Each key is the engine's own name for the symbol, `Class::member` where it has one, so
it can be looked up in upstream gamedata and in the binary; `Bindings::Bind` maps it to a plain C++
name. A key may appear in only one section.

```jsonc
{
  "$schema": "./gamedata.schema.json",
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
    "CSource2Server::g_GameEventManager": {
      "signature": "CSource2Server::Init",                   // must exist in "signatures"
      "rel32At": { "windows": 98, "linux": 106 }             // bytes from the match to the rel32
    }
  },

  // A vtable slot, plus the class whose table the index is counted in. With a "signature", the
  // slot is found by searching that class's table for the function the pattern matched, and the
  // index is only the fallback.
  "vtables": {
    "CPlayer_MovementServices::RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "CServerSideClient::ProcessRespondCvarValue": { "class": "CServerSideClient", "library": "engine2", "windows": 38, "linux": 40 }
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

- one key in two sections;
- an entry with no column for the platform being loaded;
- a malformed byte pattern;
- a negative `rel32At`, or an `addresses` or `vtables` entry naming a signature that does not exist;
- a vtable index outside `[0, 500)`;
- an offset above its `max`, or not a multiple of its `align`.

Resolution failures do not reject the file. `GameData::FailureSummary()` reports
them, and affected capabilities carry the same reason.

A vtable entry whose `signature` resolves keeps the slot that holds it, and warns when that is not
the index in the file. The search reads through another plugin's hooks, so it does not matter which
plugin loaded first. When the signature does not resolve, or its function is in no slot, the entry
keeps its index and says so.

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

Every entry can drift after a CS2 update. Treat an older `build.verified` date
as unverified.

1. **Run `voltmod gamedata check`.** It reports, against the installed binaries and in a second,
   which signatures no longer match. Prefer it to the load report: it needs no server, and it says
   why an entry drifted rather than only that it did.
2. **Run `voltmod gamedata resolve --write`** to repair what it can, then read the diff. It only
   ever widens a struct displacement that moved, and only when exactly one such change brings the
   pattern back to a single match. Anything else it refuses, and those you re-sync by hand from
   the upstream named in `gamedata.jsonc`.
3. **Re-check every vtable index that has no `signature`.** Executable-section validation catches
   invalid slots, but not a valid slot that points to the wrong function. An entry with a
   `signature` reports its own move in the load log instead; copy the new index into the file.
4. **Re-check every byte offset.** Stale offsets can read plausible unrelated data. Keep `max` and
   `align` tight.
5. **Exercise each feature on a live server.** Successful resolution does not prove correct behavior.
6. **Update `build.verified`** in the same change.

The entries most likely to bite, and how each one fails:

| Entry | Section | Used by | Drift symptom |
| --- | --- | --- | --- |
| `CPlayer_MovementServices::RunCommand` | vtables | @ref VoltMod::Movement | Crash on the first movement tick, unless the executable-section check catches it |
| `CBaseEntity::Teleport` | vtables | @ref VoltMod::Teleport | Missing: subscribing to `Teleported` is refused and `Capability::Teleport` is off |
| `CServerSideClient::ProcessRespondCvarValue` | vtables | @ref VoltMod::ClientConVars | `Capability::ClientConVars` off; client convar queries unavailable |
| `CUserCmd::CSGOUserCmdPB` | offsets | `Movement` cmd events | Missing: `Valid=false` views. Stale: garbage viewangles and buttons |
| `CUserCmdBase::cmdNum` | offsets | `PlayerInput::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which live clients leave at 0. Stale: a counter that never increments by 1 |
| `CServerSideClientBase::m_nClientSlot` | offsets | `ClientConVars`, `UiClicks` | Stale: a client's answer is attributed to the wrong player |
| `INetworkMessageProcessingPreFilter::FilterMessage` | signatures | @ref VoltMod::ScreenManager::Pressed | Missing: `Capability::UiClicks` off; presses never arrive |
| `CServerSideClient::INetworkMessageProcessingPreFilter` | offsets | @ref VoltMod::ScreenManager::Pressed | Stale: a press is attributed to the wrong player, or dropped |
| `CNetworkGameServer::ReplyConnection` | signatures | @ref VoltMod::Addons | Missing: `Capability::Addons` off; `Require` is refused |
| `CNetworkGameServer::m_szAddons` | offsets | @ref VoltMod::Addons | Stale: clients download addons but mount none, or a corrupted reply |
| `CheckTransmitPlayerSlot` | offsets | @ref VoltMod::Visibility | Stale: the wrong recipient is filtered |
| `CSource2Server::g_GameEventManager` | addresses | @ref VoltMod::Messages | Center HTML does not display |

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
    18/21 signatures hold
    REPAIRED  signatures.CustomHudSetInputCapture
              bytes 12-15: 1200 -> 1208, wildcarded
              1208 is CCSCustomHudLayout::m_vecPlayerLayoutStates
```

## Signature scanning

The internal scanner rejects ambiguous patterns and out-of-bounds rel32 targets.
Plugins use it through gamedata rather than directly.

### Vtable lookup by class name

`FindVirtualTable(moduleName, className)` resolves primary class tables. `ClassSlot` keeps each
resolved table with its slot. A function on a secondary base has no primary slot, so it is bound
by signature and hooked with `HookFunction` instead.

- Windows: walks the module's RTTI: the type descriptor for `.?AV<class>@@`, the complete object
  locator referencing it, then the vtable that follows. Only a locator at offset 0 is accepted, so
  the result is always the class's primary vtable, never a base subobject's. The name has to be the
  top-level class name exactly: a `struct` (`.?AU`), a nested class, or a namespaced one resolves to
  null.
- Linux: reads `_ZTV<mangled>` from the ELF `.symtab`, falling back to `.dynsym`. The game's
  libraries hide those symbols, so it then walks the Itanium RTTI in the mapped module.

Lookup returns null on failure. The gamedata entry still supplies the slot.

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
| `gamedata/gamedata.jsonc` | functions, vtables, interfaces | hand-maintained | at load, per entry |
| `schema/server.<platform>.json` | entity field offsets | dumped from the engine | at load, whole layout |

Schema fields are generated accessors, so a sub-object is a hop rather than a follow:

```cpp
int money = controller.InGameMoneyServices().Account();
```

Every accessor answers harmlessly on a falsy view, and a `Set` dirties the field for replication
on its own. Use them only on the game thread.
