# SDK wrappers {#sdk_guide}

[TOC]

## Overview

`VoltMod/Engine/`, `VoltMod/Entities/`, `VoltMod/Events/`, `VoltMod/Messaging/` and
`VoltMod/Hooks/` are the engine wrapper layer: typed classes over HL2SDK interfaces
so plugin code does not have to juggle raw pointers and reversed offsets. Their
names, like every other public name, live directly in `VoltMod`.

The guide is split by topic:

- @subpage sdk_gamedata_guide - signature scanning, named offsets, and runtime schema field resolution
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
auto& gi = runtime.Interfaces;
auto* engine = gi.Engine;       // IVEngineServer2*
auto* cvar = gi.CVar;           // ICvar*
auto* schema = gi.SchemaSystem; // ISchemaSystem*
// ... etc.
```

Other engine-facing classes read from this holder internally. The examples on these pages
reach services through `runtime.GameData`, `runtime.Entities`, `runtime.Schema()`,
and the corresponding runtime members.
