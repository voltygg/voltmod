# SDK wrappers {#sdk_guide}

[TOC]

## Overview

`VoltMod/Engine/`, `VoltMod/Entities/`, `VoltMod/Events/`, `VoltMod/Messaging/` and
`VoltMod/Hooks/` are the engine wrapper layer: typed classes over HL2SDK interfaces
so plugin code does not have to juggle raw pointers and reversed offsets. Their
names, like every other public name, live directly in `VoltMod`.

`<VoltMod/Api.hpp>` already carries the wrapper types `Runtime` holds by value
(`EntitySystem`, `GameEvents`, `Messages`, ...). Three more headers gather what
it does not:

| Header | Brings in |
|---|---|
| `<VoltMod/Entities/Api.hpp>` | Every frame-local wrapper (`Entity`, `Pawn`, `Controller`, `EntityRef`, `Field`, ...), `EntitySystem`, `EntityOps`, `Items`, `Render`, and `ConVar`/`ConVarLease` |
| `<VoltMod/Hooks/Api.hpp>` | Every hook (`Movement`, `Teleport`, `Transmit`, `Visibility`, `ChatInput`, `ClientCvars`, `GlowVision`, `UserCmd`), game events, and `Messages`/`CenterHtml` |
| `<VoltMod/Unsafe/Api.hpp>` | `Interfaces`, `GameData`, `Bindings`, `MemoryAccess`, `RecipientFilter`, and the vtable-hook macros - opt in only where a plugin pokes at the engine directly |

The guide is split by topic:

- @subpage sdk_gamedata_guide - the gamedata file, typed `Bindings`, capabilities, and runtime schema fields
- @subpage sdk_players_guide - entity lookup, the typed player wrapper, common pawn operations, and weapons
- @subpage sdk_entity_ops_guide - entity creation, entity IO, one-shot world effects, and resource precaching
- @subpage sdk_visibility_guide - render mode/color tricks, per-recipient transmit filtering, and per-viewer glow vision
- @subpage sdk_messaging_guide - chat/center-HTML messages, sticky panels, chat input capture, and the yes/no vote panel
- @subpage sdk_events_guide - typed ConVar access, game event listeners, and level changes
- @subpage sdk_hooks_guide - the per-tick movement and damage hooks, teleport tracking, and RAII server console commands
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

Other engine-facing classes read from this holder internally. The examples on these pages
reach services through `runtime.Entities` and the corresponding runtime members. Schema field
offsets are not among them: a @ref VoltMod::Field resolves its own, once per process.

## Capabilities

Not every wrapper can work on every build of the game: a gamedata entry can fail to resolve, an
engine interface can be missing. `Runtime::Start` records each outcome in
@ref VoltMod::Capabilities, and that is the only place to ask - no service exposes an
`Available()` or `IsResolved()` flag of its own.

```cpp
using VoltMod::Capability;

if (!runtime.Capabilities.Has(Capability::ClientCvars))
    Log::Warn("no client convar queries: {}", runtime.Capabilities.Reason(Capability::ClientCvars));

Log::Info("{}", runtime.Capabilities.Summary());  // "12/14 ok; Movement: ..."
```

The enumerators are `Schema`, `Entities`, `EntityOps`, `GameEvents`, `Movement`, `Damage`,
`Teleport`, `Transmit`, `ClientCvars`, `Precache`, `Vote`, `Items`, `Menus` and `Http`. A
capability that is off means its service is inert, not unsafe: calling into it returns
`Error::NotReady`, an empty `Subscription`, or nothing at all. The same picture is in the load
log and in the `capabilities` status section.
