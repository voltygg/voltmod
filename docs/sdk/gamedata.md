# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

## What gamedata is for

VoltMod ships `gamedata/gamedata.jsonc`, which `Runtime::Start()` loads before consumer `OnLoad`.
It answers one question: **where** in the loaded game modules a thing is. It never says **what**
that thing is - every prototype, vtable signature and field type is C++, in
@ref VoltMod::Bindings.

That split is the whole design. A gamedata key is a member of `Bindings`, typed by the framework:

```cpp
// include/VoltMod/Engine/Bindings.hpp
Fn<CEntityInstance*(const char*, int)> CreateEntityByName;   // signatures.CreateEntityByName
VFn<void(int)> ChangeTeam;                                   // vtables.ChangeTeam
OffsetOf<int> ServerSideClientSlot;                          // offsets.ServerSideClientSlot
```

Services take `const Bindings&` and read a field. Nothing looks an entry up by string on a call
path, so a renamed key is a compile error and a *missing* one is a load-time capability failure
naming the key - never a null pointer discovered at the moment it is used.

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

Wildcard bytes are `?` or `??`. `max` and `align` describe the field the framework reads at that
offset, so a hand-edited or drifted value is rejected at load rather than turning into a read of
unrelated memory; both are optional (`4096` and `1`).

`gamedata.schema.json` sits next to the file with `additionalProperties: false` everywhere, so an
editor squiggles a typo before the server ever sees it.

## What the loader checks

Parsing is separate from scanning (`src/Engine/GameDataFile.{hpp,cpp}`), so the format is checked
with no engine loaded and the checks have unit tests. A file is rejected outright - the `GameData`
load stage degrades and every capability goes off - when it has:

- no `version`, or a `version` this build does not read;
- one key in two sections;
- an entry with no column for the platform being loaded;
- a malformed byte pattern;
- a negative `rel32At`, or an `addresses` entry naming a signature that does not exist;
- a vtable index outside `[0, 500)`;
- an offset above its `max`, or not a multiple of its `align`.

Individual entries that parse but do not *resolve* (a pattern that is not found, a module that is
not mapped, a class whose vtable cannot be located) do not fail the load. Each one is recorded with
its reason, `GameData::FailureSummary()` names them in the load report, and the capability that
needed it goes off carrying the same reason.

## Capabilities, not readiness flags

Nothing exposes `Available()` / `IsResolved()` any more. `Runtime::Start` records every outcome in
@ref VoltMod::Capabilities, and that is the one place to ask:

```cpp
if (!runtime.Capabilities.Has(Capability::Movement))
    Log::Warn("no movement feed: {}", runtime.Capabilities.Reason(Capability::Movement));
```

The console log carries a one-line summary at load ("12/14 ok; Movement: RunCommand vtable
unresolved; ..."), and the `capabilities` status section carries the same thing as JSON. A service
whose capability is off is inert and safe to call: it returns `Error::NotReady`, an empty
`Subscription`, or nothing at all.

## Re-verify after an engine update

Every entry in this file drifts with CS2. `build.verified` is the date the whole file was last
walked entry by entry - if it is older than the last game update, treat the file as unverified.

1. **Read the load report and the capability summary.** Start the server and look for the
   `GameData` stage's failure list and the `Capabilities:` line. Anything named there is already
   broken; anything *not* named may still be silently wrong, which is what the rest of this
   procedure is for.
2. **Re-sync the patterns.** The comments in `gamedata.jsonc` name where each entry came from
   (CS2Fixes, SwiftlyS2, CS2AC). Pull their current gamedata for the same entry names and replace
   the patterns. A pattern that now matches twice is reported as `ambiguous` and is as bad as one
   that matches nothing - lengthen it rather than accepting the first hit.
3. **Re-check every vtable index against its upstream.** These are the dangerous ones: a wrong
   index calls an unrelated vfunc and crashes on the first use. The framework refuses a binding
   whose slot does not point into the module's executable section, which catches a table found
   under a drifted class name and an index past the end of a real table - but not an index that
   happens to land on a different real function.
4. **Re-check every byte offset.** A missing offset degrades cleanly; a *stale* one reads unrelated
   memory and looks like plausible data. Tighten `max`/`align` to what the field actually is, so a
   value that drifted far is rejected instead of read.
5. **Exercise the features on a live server**, not just at load: fire the movement hook (join and
   move), take damage from each source, run a client convar query, hide a player. Load-time success
   only proves the address resolved.
6. **Update `build.verified`** to the date you did all of the above, in the same change as the
   entries.

The entries most likely to bite, and how each one fails:

| Entry | Section | Used by | Drift symptom |
| --- | --- | --- | --- |
| `RunCommand` | vtables | @ref VoltMod::Movement | Crash on the first movement tick, unless the executable-section check catches it first |
| `OnTakeDamageAlive` | vtables | @ref VoltMod::Damage | Same, on the first point of damage |
| `Teleport` | vtables | @ref VoltMod::Teleport | Missing: subscribing to `Teleported` is refused and `Capability::Teleport` is off |
| `ProcessRespondCvarValue` | vtables | @ref VoltMod::ClientCvars | `Capability::ClientCvars` off; client convar queries unavailable |
| `UserCmdPB` | offsets | `Movement` cmd events | Missing: `Valid=false` views. Stale: garbage viewangles and buttons |
| `UserCmdNumber` | offsets | `UserCmdView::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which live clients leave at 0. Stale: a counter that never increments by 1 |
| `ServerSideClientSlot` | offsets | `ClientCvars` | Stale: a client's answer is attributed to the wrong player |
| `CheckTransmitPlayerSlot` | offsets | @ref VoltMod::Transmit | Stale: the wrong recipient is filtered |
| `TakeDamageInfo*` | offsets | `Damage` | Missing: the hook refuses to install. Stale: plausible-looking nonsense in every `DamageView` |
| `GameEventManager` | addresses | @ref VoltMod::Messages | Center HTML does not display |

## Signature scanning

The byte-pattern scanner is **internal** (`src/Engine/SigScanner.hpp`); consumers reach it through
gamedata rather than calling it. It keeps scanning past the first hit so an ambiguous pattern is
reported instead of silently taken, and a rel32 resolve is bounds-checked against the module image -
the arithmetic itself is in the public, engine-free `<VoltMod/Engine/RelativeAddress.hpp>`, which is
where its tests live.

### Vtable lookup by class name

`src/Engine/VtableLookup.hpp` (`FindVirtualTable(moduleName, className)`) is the second internal
resolver. It exists because a *class vtable* hook (SourceHook's `SH_ADD_MANUALDVPHOOK`) covers every
instance at once, where a per-instance hook has to be re-bound as objects come and go. Three
bindings need it - `Bindings::MovementServices`, `Bindings::PlayerPawn` and
`Bindings::ServerSideClient` - and each takes its class name from the `class` field of the vtable
entry it belongs to, never from a C++ literal.

- Windows: walks the module's RTTI - the type descriptor for `.?AV<class>@@`, the complete object
  locator referencing it, then the vtable that follows. Only a locator at offset 0 is accepted, so
  the result is always the class's primary vtable, never a base subobject's. The name has to be the
  top-level class name exactly: a `struct` (`.?AU`), a nested class, or a namespaced one resolves to
  null.
- Linux: reads `_ZTV<mangled>` from the ELF `.symtab`, falling back to `.dynsym`. A fully stripped
  module has neither.

Both return null on failure rather than a wrong answer, and the binding then fails with a reason
that reaches the capability. The vtable *index* still comes from the same entry; the lookup only
finds the table.

## Schema fields

Schema offsets are a separate mechanism: the engine publishes them at runtime, so they need no
gamedata and do not drift the same way. They are also constants of the loaded server binary, which
is why they are not a service - a @ref VoltMod::Field resolves its own once per `(class, field)`
for the whole process, walking base classes and caching misses as well as hits:

```cpp
runtime.Entities.PawnOf(slot).Health = 100;   // resolves CBaseEntity::m_iHealth once, then writes
```

`Field` passes `sizeof(T)` as the expected size, so the first lookup validates it against the
engine's own field size and warns "is N bytes but the caller reads M (schema drift?)" once when a
game update retypes a field. It still returns the offset.

For a field with no wrapper to hang it on - one inside an engine sub-object, or a one-off internal
lookup - declare a `static` @ref VoltMod::LazyField beside the code that reads it:

```cpp
static const VoltMod::LazyField kItemServices{"CBasePlayerPawn", "m_pItemServices", sizeof(void*)};

if (kItemServices)
    services = VoltMod::ReadAt<void*>(pawn.Raw(), kItemServices->Offset);
```

Both retry for as long as the schema system is not up yet, so a lookup made during load does not
freeze an empty answer for the rest of the session. Everything here is game-thread only.
