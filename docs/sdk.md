# SDK wrappers {#sdk_guide}

[TOC]

The Engine, Entities, Events, Messaging, Hooks and Ui modules wrap HL2SDK and reverse-engineered
engine access in typed APIs. Public names live directly in `VoltMod`.

`<VoltMod/Api.hpp>` carries the wrapper types `Runtime` holds by value. Three module headers gather
what it does not:

| Header | Brings in |
| --- | --- |
| `<VoltMod/Entities/Api.hpp>` | `Entity`, `Pawn`, `Controller`, `EntityRef`, `EntitySystem`, `KeyValues`, `Pawns`, `PawnOps`, `Trace`, `ConVar`, `ConVarOverrides` |
| `<VoltMod/Hooks/Api.hpp>` | `Movement`, `PlayerInput`, `Teleport`, `Damage`, `Visibility`, `GlowVision`, `ChatInput`, `ClientConVars`, `Vote`, `GameEvents` and the event structs, `Messages`, `CenterHtml` |
| `<VoltMod/Unsafe/Api.hpp>` | `Interfaces`, `Bindings`, `MemoryAccess`, `RecipientFilter` and the hook entry points - opt in only where a plugin pokes at the engine directly |

## Pages

| Page | Covers |
| --- | --- |
| @subpage sdk_gamedata_guide | `gamedata.jsonc`, typed `Bindings`, the schema layout, re-verifying after a game update |
| @subpage sdk_players_guide | entity lookup, the wrapper contract, schema fields, pawn operations, spawning and effects |
| @subpage sdk_visibility_guide | render mode and color, per-recipient visibility filtering, per-viewer glow |
| @subpage sdk_messaging_guide | chat, center HTML, sticky panels, chat input capture, the yes/no vote panel |
| @subpage sdk_events_guide | typed convars, game event listeners, level changes |
| @subpage sdk_hooks_guide | movement hooks, teleport tracking, damage, custom vtable hooks, server console commands |
| @subpage sdk_client_telemetry_guide | the simulation clock, per-client latency, client convar queries |

## Interfaces

`Runtime::Initialize()` fills `runtime.Unsafe.Interfaces` with the SDK interfaces the framework uses, so
they are live when the plugin is constructed:

```cpp
#include <VoltMod/Unsafe/Api.hpp>

auto* engine = runtime.Unsafe.Interfaces.Engine;  // IVEngineServer2*
auto* cvar = runtime.Unsafe.Interfaces.CVar;      // ICvar*
```

Prefer the typed runtime services; reach for this holder only for an engine call the framework has
not wrapped.

## Available()

A CS2 update can invalidate the gamedata a feature needs. Those features carry `Available()`, which
returns the reason when they cannot work:

```cpp
if (auto available = runtime.Hooks.ClientConVars.Available(); !available)
    Log::Warn("no client convar queries: {}", available.error().Detail);
```

`Available()` is on `Hooks.Movement`, `Hooks.Teleport`, `Hooks.Damage`, `Hooks.Visibility`,
`Hooks.ClientConVars`, `World.Trace` and `Screens`. A service that is not available stays safe to call and returns an
error, an empty `Subscription`, or no result. `Runtime::Initialize` logs every unavailable feature once,
and the `load` status section lists them.
