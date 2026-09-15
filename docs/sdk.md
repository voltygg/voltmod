# SDK wrappers {#sdk_guide}

[TOC]

## Overview

The Engine, Entities, Events, Messaging, and Hooks modules wrap HL2SDK and
reverse-engineered engine access in typed APIs. Public names live directly in
`VoltMod`.

`<VoltMod/Api.hpp>` already carries the wrapper types `Runtime` holds by value
(`EntitySystem`, `GameEvents`, `Messages`, ...). Three more headers gather what
it does not:

| Header | Brings in |
|---|---|
| `<VoltMod/Entities/Api.hpp>` | Every frame-local wrapper (`Entity`, `Pawn`, `Controller`, `EntityRef`, ...), `EntitySystem`, `EntityOps`, `Items`, `Render`, and `ConVar`/`ConVarOverrides` |
| `<VoltMod/Hooks/Api.hpp>` | Every hook (`Movement`, `Teleport`, `Visibility`, `ChatInput`, `ClientConVars`, `GlowVision`, `PlayerInput`), game events, and `Messages`/`CenterHtml` |
| `<VoltMod/Unsafe/Api.hpp>` | `Interfaces`, `GameData`, `Bindings`, `MemoryAccess`, `RecipientFilter`, and the vtable hooks - opt in only where a plugin pokes at the engine directly |

The guide is split by topic:

- @subpage sdk_gamedata_guide - the gamedata file, typed `Bindings`, capabilities, and runtime schema fields
- @subpage sdk_players_guide - entity lookup, the typed player wrapper, common pawn operations, and weapons
- @subpage sdk_entity_ops_guide - entity creation, entity IO, one-shot world effects, and resource precaching
- @subpage sdk_visibility_guide - render mode/color tricks, per-recipient visibility filtering, and per-viewer glow vision
- @subpage sdk_messaging_guide - chat/center-HTML messages, sticky panels, chat input capture, and the yes/no vote panel
- @subpage sdk_events_guide - typed ConVar access, game event listeners, and level changes
- @subpage sdk_hooks_guide - movement hooks, teleport tracking, custom vtable hooks, and server console commands
- @subpage sdk_client_telemetry_guide - the server clock, per-client latency, and client convar queries

## Interfaces

`Runtime::Start()` populates this holder with the SDK interfaces used by the
framework.

```cpp
#include <VoltMod/Runtime.hpp>

// OnLoad runs after Runtime::Start, so the interfaces are available here:
auto& gi = runtime.Unsafe.Interfaces;
auto* engine = gi.Engine;       // IVEngineServer2*
auto* cvar = gi.CVar;           // ICvar*
auto* schema = gi.SchemaSystem; // ISchemaSystem*
// ... etc.
```

Framework services use this holder internally. Plugin code normally uses typed
runtime services instead. Schema fields resolve their own offsets once per
process.

## Availability

Engine updates may invalidate gamedata or remove an interface. A service whose engine pieces did
not bind says so from `Available()`, with the reason:

```cpp
if (auto available = runtime.Hooks.ClientConVars.Available(); !available)
    Log::Warn("no client convar queries: {}", available.error().Detail);
```

`Available()` is on `Hooks.Movement`, `Hooks.Teleport`, `Hooks.Visibility`, `Hooks.ClientConVars`
and `Screens`. Other services return an error from the call that cannot work. A service that is
not available remains safe to call. The load log and the `load` status section list the
unavailable features.
