# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

## GameData (signature management)

VoltMod ships `gamedata/signatures.jsonc`, which `Runtime::Start()` loads before
consumer `OnLoad`. It contains engine signatures and offsets used by SDK
services.

Consumer plugins can also use GameData for their own signatures:

```cpp
auto& gd = runtime.GameData;

// Find a raw signature address
void* addr = gd.FindSignature("CCSPlayerController_Kick");

// Resolve a signature with RIP-relative offset
void* resolved = gd.ResolveSignature("SomeFunction");
```

### Eager resolution and diagnostics

`Runtime::Start()` runs `ResolveAll()` as the `GameData` load stage. Each
signature is scanned once, and later lookups use the cache. The stage reports
missing and ambiguous signatures by name; per-entry results are available from
`Resolutions()`.

### `signatures.jsonc` format

```json
{
    "CCSPlayerController_Kick": {
        "windows": {
            "pattern": "48 89 5C 24 ?? 57 48 83 EC 30",
            "offset": 0
        },
        "linux": {
            "pattern": "55 48 89 E5 41 54 49 89 FC",
            "offset": 0
        }
    }
}
```

## Named offsets

The `"offsets"` block of `signatures.jsonc` holds per-platform integers rather than patterns, resolved by name and with no scanning involved:

```cpp
int index = runtime.GameData.GetVtableIndex("RunCommand");           // negative when missing or out of range
int offset = runtime.GameData.GetByteOffset("UserCmdPB", MaxUserCmdOffset, alignof(void*));
```

Two kinds live in there, and they fail differently:

- Vtable indexes (`RunCommand`, `Teleport`, `ProcessRespondCvarValue`, ...), read with `GetVtableIndex`. A wrong index dispatches into whatever vfunc sits at that slot, which is a crash, not a wrong answer.
- Byte offsets into an undeclared layout (`UserCmdPB`, `ServerSideClientSlot`, `CheckTransmitPlayerSlot`, ...), read with `GetByteOffset`. These are fields the SDK headers do not declare, so the framework reaches them by distance. A *missing* one degrades cleanly; a *stale* one reads unrelated memory and looks like plausible data.

Both accessors reject the entry and warn instead of returning it: every vtable index above 500
(`MaxVtableIndex`) and every byte offset above 4096 (`MaxByteOffset`) or not a multiple of the
caller's alignment is rejected at lookup, so the owning load stage degrades instead of dispatching
into the wrong vfunc or reading unrelated memory. A caller can pass a tighter ceiling, as
`Movement` does for `UserCmdPB`/`UserCmdNumber` with `MaxUserCmdOffset`. This catches invalid
edits, but not a stale value that still falls within the accepted range - game updates still require
verification.

Everything here **drifts with CS2 updates**. Re-verify every entry after a game
update against the upstream projects named in its comments (SwiftlyS2, CS2Fixes,
and CS2AC). The entries surfaced by anti-cheat are:

| Offset | Used by | Drift symptom |
|--------|---------|---------------|
| `RunCommand` | @ref VoltMod::Movement | Crash on the first movement tick |
| `UserCmdPB` | `Movement` cmd listeners | Missing: `Valid=false` views. Stale: garbage viewangles/buttons |
| `UserCmdNumber` | `UserCmdView::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which the live client leaves at 0. Stale: a counter that never increments by 1 |
| `Teleport` | @ref VoltMod::Teleport | `Enable()` returns false when missing |
| `ProcessRespondCvarValue` | @ref VoltMod::ClientCvars | Rejected at lookup, so the load stage degrades instead of crashing |
| `ServerSideClientSlot` | `ClientCvars` | Rejected at lookup too; unchecked it would attribute a client's answer to the wrong player |

`ClientCvars::Initialize()` leaves the service inert when either lookup fails.

## Signature scanning

The low-level byte-pattern scanner is **internal** (`src/Engine/SigScanner.hpp`, free functions
`FindPattern(moduleName, pattern)` / `ResolveRelativeAddress(addr, ripOffset, ripSize)`); it is not
part of the public include tree. Consumers scan through @ref VoltMod::GameData instead, which
adds per-platform patterns, named lookups, and caching:

```cpp
auto& gd = runtime.GameData;
void* addr = gd.FindSignature("CCSPlayerController_Kick");     // raw match
void* resolved = gd.ResolveSignature("SomeFunction");          // + RIP-relative resolve
```

Wildcard bytes are written as `?` or `??` in pattern strings (see the signatures.jsonc format above).

### Vtable lookup by class name

`src/Engine/VtableLookup.hpp` (`FindVirtualTable(moduleName, className)`) is the second internal
resolver, and like the scanner it is not in the public include tree. It exists because a *class
vtable* hook (SourceHook's `SH_ADD_MANUALDVPHOOK`) covers every instance at once, where a per-instance
hook has to be re-bound as objects come and go. Two services need that:
@ref VoltMod::ClientCvars for `CServerSideClient` in `engine2`, and
@ref VoltMod::Movement for `CCSPlayer_MovementServices` in `server` - neither owns the
instances, and the movement hook must install before any pawn exists.

It shares `FindModuleImage` with the scanner and resolves per platform:

- Windows: walks the module's RTTI, first the type descriptor for `.?AV<class>@@`, then the complete
  object locator referencing it, then the vtable that follows. This assumes the module was built with RTTI.
- Linux: reads `_ZTV<mangled>` from the ELF `.symtab`, falling back to `.dynsym`. A fully stripped
  module has neither and resolves to null.

The name has to be the top-level class name, exactly: the Windows decoration `.?AV<name>@@` matches
only a non-template `class`, so a `struct` (`.?AU`), a nested class, or a namespaced one resolves to
null there. Only a locator at offset 0 is accepted, so the result is always the class's primary
vtable, never a base subobject's.

Both platforms return `nullptr` on failure rather than a wrong answer, and callers must degrade:
`ClientCvars` logs and stays inert, leaving `Available()` false; `Movement::Install()`
returns false. The vtable *index* to hook still comes from the `"offsets"` block above. The lookup
only finds the table, never the slot within it.

## SchemaService

Resolves entity field offsets at runtime using CS2's schema system. Results are cached:

```cpp
auto& schema = runtime.Schema();

// Get field offset
int32_t offset = schema.GetOffset("CCSPlayerPawn", "m_iHealth");

// Pass the expected size for fixed-size reads/writes: the first lookup validates it
// against the engine's field size and warns "is N bytes but the caller expects M
// (schema drift?)" after a game update changes a field type; still returns the offset.
int32_t checked = schema.GetOffset("CCSPlayerPawn", "m_iHealth", sizeof(int));

// Use with entity pointer
int health = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(pawn) + offset);
```

`PlayerController`'s typed field templates (`GetField<T>`, `GetPawnField<T>`, `SetField<T>`, `SetPawnField<T>`) pass `sizeof(T)` through automatically.
