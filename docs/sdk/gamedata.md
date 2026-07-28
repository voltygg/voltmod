# Gamedata & Schema {#sdk_gamedata_guide}

[TOC]

## GameData (Signature Management)

CS2-Kit ships built-in gamedata (`gamedata/signatures.jsonc`) that is automatically loaded during `CS2Kit::Initialize()`. The gamedata contains engine signatures and offsets used internally by Entity, PlayerController, and UserMessage subsystems.

Consumer plugins can also use GameData for their own signatures:

```cpp
auto& gd = Engine().GameData;

// Find a raw signature address
void* addr = gd.FindSignature("CCSPlayerController_Kick");

// Resolve a signature with RIP-relative offset
void* resolved = gd.ResolveSignature("SomeFunction");
```

### Eager resolution and diagnostics

`CS2Kit::Initialize()` runs `ResolveAll()` as the `GameData` load stage: every signature is scanned once, `FindSignature`/`ResolveSignature` answer from the cache afterwards, and the stage reports failures by name (`"2/13 signatures failed: X, Y"`). The scanner also detects **ambiguous** patterns - a pattern matching more than one location is a broken signature waiting to resolve to the wrong function after a game update, so it is warned about and listed in the stage detail. Per-entry results are available programmatically via `Resolutions()`.

### Deliberately not implemented

Two s2sdk-style mechanisms were evaluated and rejected for now; revisit if an engine update actually burns us:

- **Ref-anchored fallback** (find functions by referenced strings when a pattern breaks): high machinery cost for a gamedata file this small whose signatures are synced from upstream projects with provenance comments.
- **RTTI vtable-by-name resolution**: current consumers are index-based vcalls whose indexes historically break less often than byte patterns.

### signatures.jsonc Format

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
int index = Engine().GameData.GetOffset("RunCommand");   // negative when the entry is missing
```

Two kinds live in there, and they fail differently:

- **Vtable indexes** (`RunCommand`, `Teleport`, `ProcessRespondCvarValue`, ...). A wrong index dispatches into whatever vfunc sits at that slot, which is a crash, not a wrong answer.
- **Byte offsets into an undeclared layout** (`UserCmdPB`, `ServerSideClientSlot`, `CheckTransmitPlayerSlot`, ...) - fields the SDK headers do not declare, so the kit reaches them by distance. A *missing* one degrades cleanly; a *stale* one reads unrelated memory and looks like plausible data.

Everything here **drifts with CS2 updates**, so re-verify after every one against the upstream projects named in the per-entry comments (SwiftlyS2, CS2Fixes, CS2AC), which are the provenance for these values. The entries the anti-cheat surfaces depend on:

| Offset | Used by | Drift symptom |
|--------|---------|---------------|
| `RunCommand` | @ref CS2Kit::Sdk::MovementHook | Crash on the first movement tick |
| `UserCmdPB` | `MovementHook` cmd listeners | Missing: `Valid=false` views. Stale: garbage viewangles/buttons |
| `UserCmdNumber` | `UserCmdView::CommandNumber` | Missing: falls back to the protobuf's `legacy_command_number`, which the live client leaves at 0. Stale: a counter that never increments by 1 |
| `Teleport` | @ref CS2Kit::Sdk::TeleportTracker | `Enable()` returns false when missing |
| `ProcessRespondCvarValue` | @ref CS2Kit::Sdk::ClientCvarService | Sanity-bounded at init (index > 500 rejected), so the load stage degrades instead of crashing |
| `ServerSideClientSlot` | `ClientCvarService` | Sanity-bounded too (offset > 4096 or misaligned rejected); unchecked it would attribute a client's answer to the wrong player |

The two `ClientCvarService` entries are validated at `Initialize()` precisely because they come from a third-party gamedata file and can be hand-edited: an implausible value logs a warning and leaves the service inert rather than taking the server down. That safety net catches nonsense, not a value that drifted to another *plausible* index - only re-verification does.

## Signature scanning

The low-level byte-pattern scanner is **internal** (`src/Sdk/SigScanner.hpp`, free functions
`FindPattern(moduleName, pattern)` / `ResolveRelativeAddress(addr, ripOffset, ripSize)`); it is not
part of the public include tree. Consumers scan through @ref CS2Kit::Sdk::GameData instead, which
adds per-platform patterns, named lookups, and caching:

```cpp
auto& gd = Engine().GameData;
void* addr = gd.FindSignature("CCSPlayerController_Kick");     // raw match
void* resolved = gd.ResolveSignature("SomeFunction");          // + RIP-relative resolve
```

Wildcard bytes are written as `?` or `??` in pattern strings (see the signatures.jsonc format above).

## SchemaService

Resolves entity field offsets at runtime using CS2's schema system. Results are cached:

```cpp
auto& schema = Engine().Schema();

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
