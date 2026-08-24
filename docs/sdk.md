# SDK wrappers {#sdk_guide}

[TOC]

## Overview

`VoltMod::Sdk` is the engine wrapper layer: typed classes over HL2SDK interfaces
so plugin code does not have to juggle raw pointers and reversed offsets.

The guide is split by topic:

- @subpage sdk_gamedata_guide - signature scanning, named offsets, and runtime schema field resolution
- @subpage sdk_players_guide - entity lookup, the typed player wrapper, and common pawn operations
- @subpage sdk_entity_ops_guide - entity creation, entity IO, one-shot world effects, and resource precaching
- @subpage sdk_visibility_guide - render mode/color tricks, per-recipient transmit filtering, and per-viewer glow vision
- @subpage sdk_messaging_guide - chat/center-HTML messages, sticky panels, and chat input capture
- @subpage sdk_events_guide - typed ConVar access and game event listeners
- @subpage sdk_hooks_guide - the per-tick movement hook, teleport tracking, and RAII server console commands
- @subpage sdk_client_telemetry_guide - the server clock, per-client latency, and client convar queries

## GameInterfaces

Centralized holder for all SDK interface pointers. Automatically populated by `VoltMod::Initialize()` - no manual setup needed.

```cpp
#include <VoltMod/Runtime.hpp>

// After VoltMod::Initialize(), all interfaces are available:
auto& gi = runtime.Interfaces;
auto* engine = gi.Engine;       // IVEngineServer2*
auto* cvar = gi.CVar;           // ICvar*
auto* schema = gi.SchemaSystem; // ISchemaSystem*
// ... etc.
```

Other SDK classes read from this holder internally. The examples on these pages
reach services through `runtime.GameData`, `runtime.Entities`, `runtime.Schema()`,
and the corresponding runtime members.
