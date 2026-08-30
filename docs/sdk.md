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
| `<VoltMod/Hooks/Api.hpp>` | Every hook (`Movement`, `Teleport`, `Transmit`, `Visibility`, `ChatInput`, `ClientCvars`, `GlowVision`, `UserCmd`), game events, and `Messages`/`CenterHtml` |
| `<VoltMod/Unsafe/Api.hpp>` | `Interfaces`, `GameData`, `Bindings`, `MemoryAccess`, `RecipientFilter`, and the vtable-hook macros - opt in only where a plugin pokes at the engine directly |

The guide is split by topic:

- @subpage sdk_gamedata_guide - the gamedata file, typed `Bindings`, capabilities, and runtime schema fields
- @subpage sdk_players_guide - entity lookup, the typed player wrapper, common pawn operations, and weapons
- @subpage sdk_entity_ops_guide - entity creation, entity IO, one-shot world effects, and resource precaching
- @subpage sdk_visibility_guide - render mode/color tricks, per-recipient transmit filtering, and per-viewer glow vision
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

Framework services use this holder internally. Plugin code normally uses the
typed runtime services instead. Schema fields resolve their own offsets once per
process.

## Capabilities

Engine updates may invalidate gamedata or remove an interface. `Runtime::Start`
records each result in @ref VoltMod::Capabilities; services do not expose
separate readiness flags.

```cpp
using VoltMod::Capability;

if (!runtime.Capabilities.Has(Capability::ClientCvars))
    Log::Warn("no client convar queries: {}", runtime.Capabilities.Reason(Capability::ClientCvars));

Log::Info("{}", runtime.Capabilities.Summary());  // "12/14 ok; Movement: ..."
```

The enumerators are `Schema`, `Entities`, `EntityOps`, `GameEvents`, `Movement`,
`Teleport`, `Transmit`, `ClientCvars`, `Precache`, `Vote`, `Items`, `Menus`,
`Http`, `CustomUi`, `UiClicks`, and `Addons`. A disabled service remains safe to
call and reports not-ready status, an empty subscription, or no result. The load
log and `capabilities` status section show the same state.
