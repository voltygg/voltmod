# Gamedata and schema {#sdk_gamedata_guide}

[TOC]

Gamedata says where engine code and untyped fields live. The schema says where entity fields live.
Both are resolved once and read from there.

| | says where | source | checked |
| --- | --- | --- | --- |
| `gamedata/gamedata.jsonc` | functions, globals, vtable slots, byte offsets | hand-maintained | by the host at startup, per entry |
| `schema/server.<platform>.json` | entity field offsets | dumped from the running engine | at load, whole layout |

## Bindings

The host reads `addons/voltmod/gamedata/gamedata.jsonc` and scans for every entry once, before any
plugin loads. @ref VoltMod::Bindings gives those locations C++ types:

```cpp
// include/VoltMod/Engine/GameData/Bindings.hpp
Fn<CEntityInstance*(const char*, int)> CreateEntityByName;   // functions."CreateEntityByName"
Address GameEventManager;                                    // globals."CSource2Server::g_GameEventManager"
VirtualFn<void(CEntityInstance*, int)> ChangeTeam;           // vtables."CCSPlayerController::ChangeTeam"
OffsetOf<int> ClientSlot;                                    // offsets."CServerSideClientBase::m_nClientSlot"
```

Each plugin's `Runtime::Initialize()` calls `Bindings::Bind`, which takes every member in one pass from
a @ref VoltMod::GameDataLookup over what the host already resolved. Services then hold
`const Bindings&`, so no call path does a string lookup. No plugin reads the file or scans memory
itself: a signature a game update broke costs one scan and one error line for the whole server.

`Bindings::Failures` lists `key: reason` for every member the last `Bind` left unbound.

## gamedata.jsonc format

Four sections, named for what they bind. A key is the engine's own name for the symbol, or
`Class::member`, so it can be checked against upstream gamedata and against the binary. Each key
belongs to exactly one section.

```jsonc
{
  "$schema": "./gamedata.schema.json",
  // steam.inf ServerVersion the entries were last checked on, and the date.
  "build": { "server": "2000908", "verified": "2026-09-11" },

  // A byte pattern matching the start of a function, or the function a VScript binding calls.
  "functions": {
    "CreateEntityByName": {
      "module": "server",                                  // default; "engine2" for engine code
      "windows": "48 83 EC 48 C6 44 24 30 00",
      "linux": "48 8D 05 ? ? ? ? 55 48 89 FA"
    },
    "CBaseEntity::EmitSoundParams": { "class": "CBaseEntity", "script": "EmitSoundParams" }
  },

  // A global reached through the rel32 displacement `rel32At` bytes after the match.
  "globals": {
    "CSource2Server::g_GameEventManager": {
      "windows": { "pattern": "48 8B 0D ? ? ? ? E8 ? ? ? ? 45 33 FF 4C 39 7D 50", "rel32At": 3 },
      "linux": { "pattern": "48 8D 05 ? ? ? ? 48 8B 38 E8 ? ? ? ? 49 83 7D 50 00", "rel32At": 3 }
    }
  },

  // A vtable slot, counted in the primary table of `class`, or in the table of its base `base`,
  // or the slot a virtual VScript binding dispatches through.
  "vtables": {
    "CPlayer_MovementServices::RunCommand": { "class": "CCSPlayer_MovementServices", "windows": 25, "linux": 26 },
    "CServerSideClient::ProcessRespondCvarValue": { "class": "CServerSideClient", "module": "engine2", "windows": 38, "linux": 40 },
    "CCSPlayerController::ChangeTeam": { "class": "CCSPlayerController", "script": "SetTeam" }
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

Wildcard bytes are `?` or `??`. A `class` name is the top-level RTTI name, with no namespace or
template. An offset entry is either per-platform numbers or a `class` + `base` pair read from RTTI
at load, never both.

A `script` entry has no platform columns. The host reads `class`'s VScript description, searches
its bindings and then its bases' for the name, and takes the function, or for a vtable entry the
slot the binding dispatches through. A VScript binding can vanish in an update, so use one only
where the binding calls the engine function itself.

`gamedata.schema.json` sits beside the file with `additionalProperties: false` everywhere, so an
editor flags a typo before the server sees it.

## What the host checks

The file is read strictly: an unknown key, a wrong value type, or a section that is not an object
rejects the whole file, and the host then has nothing to serve.

Otherwise every entry is resolved and each failure is logged once with its reason:

- the key is missing, is in two sections, or is in a section that member does not bind from;
- the entry has no column for this platform;
- a pattern is empty, its module is not loaded, or it matches nowhere or more than once;
- a global's rel32 displacement, or the address it points at, falls outside its module;
- an offset or index is negative;
- a vtable's class table is not found, or its slot does not hold code;
- a `script` binding is not in the class or its bases, or is virtual where a function is wanted,
  or not virtual where a slot is;
- a `base` is not in its class through RTTI, is in it more than once, is virtual, or has no vtable
  of its own.

Each plugin's `GameData` load step then lists the failures touching its own members, and each
feature reports its own through `Available()`.

A pattern is validated by matching exactly once. A vtable index or a byte offset cannot be
validated that way, so when `build.server` does not match the running server the load warns that
those columns are unchecked on this build. The log records each vtable slot's code as
`key=module+offset`, to match a crash dump against a binding.

When every entry resolves, the host writes `addons/voltmod/gamedata/resolved.<platform>.json`:
the server build, module-relative addresses for functions, globals and class tables, plus slot
indices and offsets. It is written once per server build, and a failed write does not fail the
load. Keep the record from a known-good build to diff against after an update.

## Schema fields

Schema offsets need no gamedata. `voltmod framework schemagen` bakes them into generated accessors, and the
host verifies the whole generated layout against the live schema once:

```cpp
runtime.Entities.PawnOf(slot).SetHealth(100);   // CBaseEntity::m_iHealth at a baked offset
```

`schemagen` reads `schema/manifest.json` plus a dump and writes
`include/VoltMod/Schema/Generated/` and `src/Schema/Generated/<platform>/`. The dump comes from the
engine's network serializers, which exist only while a map runs, so the host writes
`addons/voltmod/schema/server.json` at map start and skips the write when the file on disk already
carries the running game build.

Windows and Linux lay entity classes out differently. Each platform has its own committed baseline
in `schema/server.<platform>.json` and its own generated sources, so run `schemagen` once per
platform. Re-rendering a platform from its baseline needs no server:

```bash
voltmod framework schemagen --dump schema/server.windows.json --platform windows
```

Field access on a wrapper is what @ref sdk_players_guide "Entities and players" describes.

### The layout stamp

The offsets are compiled into each plugin's own copy of the SDK, so the host can only vouch for a
plugin built from the same generated layout. `schemagen` emits `GeneratedLayoutStamp()`, a hash of
class names and sizes, field names, offsets and sizes: regenerating an unchanged layout keeps the
value, and a moved field changes it. Each plugin compares its stamp with the host's before taking
the host's answer, and refuses the load when they differ:

```text
SchemaLayout: this plugin was built against another schema layout (plugin 0xECF0B3522A6F1551,
host 0x4C1E2A77B09D3E08); rebuild it against this VoltMod
```

The plugin binary and `voltmod.dll` came from different builds of the framework. Rebuild the plugin
against the VoltMod the host was built from and install both together.

When the stamps match but the live schema moved a field, the load aborts with:

```text
SchemaLayout: the host found schema drift; its log names every field
```

and the host's log names each one:

```text
CCSPlayerPawn::m_ArmorValue: offset 4828 -> 4820
```

## After a game update

`voltmod doctor --server <dir>` says when the server is behind Steam, and when gamedata was
checked on another build than the server runs. `voltmod serve` puts back the Metamod line an update
removes from `gameinfo.gi`.

A dump needs a running map, so a cold start refuses every plugin before the first map loads. Update
day is: start the server, let the plugins refuse, let a map load so the host writes the dump, run
`voltmod framework schemagen`, review the `git diff` of the generated code, rebuild.

Gamedata is repaired separately, and offline:

1. `voltmod framework gamedata fetch` downloads the new build's `server` and `engine2` binaries for both
   platforms into `~/.voltmod/cs2-builds/<build>/<platform>` (`CS2_BUILD_ARCHIVE` moves it). It
   also files the local server's `resolved.<platform>.json` under the archived build it names, so
   run it before updating the server. Steam serves only the current build, so this archive is the
   only way to compare an update with the build before it.
2. `voltmod framework gamedata check --game-dir ~/.voltmod/cs2-builds/<build>/<platform>` reports which
   `functions` and `globals` patterns no longer match those binaries, and why. It needs no server.
3. `voltmod framework gamedata check --fix` repairs what it can, then read the diff.
4. Re-check every vtable index by hand. The slot check catches an index landing on data, not
   a valid slot holding the wrong function. `script` entries need no check: the load log says when
   a binding is gone.
5. Re-check every byte offset by hand. A stale offset reads plausible unrelated data.
6. Exercise each feature on a live server. Resolution is not correctness.
7. Update `build.server` and `build.verified` in the same change.

`check` takes `--game-dir` (default `CS2_SERVER_PATH`) and `--platform`. The platform otherwise
follows whichever binaries that directory holds.

What `--fix` will and will not do:

- An entry that still matches once is never rewritten. The file changes only where it was wrong.
- A missed entry is repaired only by widening one struct displacement - bytes a pattern should
  never have pinned - and only when exactly one such change brings it back to a single match. Two
  viable candidates means nothing can say which drifted, so it refuses.
- It never searches for a function. Re-sync those by hand from the upstream named in the file.

```text
==> gamedata windows (game build 2000908)
    21/22 patterns hold
    REPAIRED  functions.CCSCustomHudLayout::SetInputCaptureEnabled
              bytes 12-15: 1200 -> 1208, wildcarded
              1208 is CCSCustomHudLayout::m_vecPlayerLayoutStates
```

Common drift points:

| Entry | Section | Used by | Drift symptom |
| --- | --- | --- | --- |
| `CPlayer_MovementServices::RunCommand` | vtables | @ref VoltMod::Movement | Crash on the first movement tick, unless the slot check catches it |
| `CBaseEntity::Teleport` | vtables | @ref VoltMod::Teleport | Subscribing to `Teleported` is refused; `Teleport::Available` says why |
| `CServerSideClient::ProcessRespondCvarValue` | vtables | @ref VoltMod::ClientConVars | `ClientConVars::Available` fails; queries unavailable |
| `INetworkMessageProcessingPreFilter::FilterMessage` | vtables (base) | @ref VoltMod::ScreenManager | Presses never arrive; a stale index hooks a different filter |
| `CUserCmd::CSGOUserCmdPB` | offsets | @ref VoltMod::Movement | Missing: `Valid=false`. Stale: garbage viewangles and buttons |
| `GameEntitySystem` | offsets | @ref VoltMod::EntitySystem | Stale: the pointer is not a `CGameEntitySystem`, so lookups return nothing |
| `CUserCmdBase::cmdNum` | offsets | `PlayerInput::CommandNumber` | Missing: falls back to `legacy_command_number`, which live clients leave at 0 |
| `CServerSideClientBase::m_nClientSlot` | offsets | `ClientConVars`, screen presses | Stale: a client's answer is attributed to the wrong player |
| `CheckTransmitPlayerSlot` | offsets | @ref VoltMod::Visibility | Stale: the wrong recipient is filtered |
| `CNetworkGameServer::ReplyConnection` | functions | @ref VoltMod::Addons | `Require` is refused with the reason |
| `CNetworkGameServer::m_szAddons` | offsets | @ref VoltMod::Addons | Stale: it no longer holds what `GetAddonName` returns, so each reply logs it and mounts nothing |
| `CSource2Server::g_GameEventManager` | globals | @ref VoltMod::GameEvents | Events do not fire and center HTML does not display |

## Class table lookup

The host finds a class vtable by RTTI on Windows and by `_ZTV` symbol - falling back to the mapped
module's Itanium RTTI - on Linux. A `class` name must therefore be the top-level RTTI name exactly;
a nested or namespaced name resolves to nothing and the entry does not bind.
