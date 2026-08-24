# Spawning & Effects {#sdk_entity_ops_guide}

[TOC]

## EntityOps (spawning, entity IO, sound)

@ref VoltMod::Sdk::EntityOpsService (`runtime.EntityOps`) exposes the signature-resolved entity
mutation functions: create/spawn, inputs, deferred IO, removal, model, and sound-event emission.
Spawn keyvalues are built with @ref VoltMod::Sdk::EntityKeyValues, which the engine consumes on
spawn (never reuse or free one after `DispatchSpawn`). Every method no-ops gracefully when its
gamedata signature failed to resolve; branch on `CanSpawn()` when you want an explicit fallback.

```cpp
auto& ops = runtime.EntityOps;

VoltMod::EntityKeyValues kv;
kv.Set("origin", pos).Set("spawnflags", 1);
if (auto* boom = ops.Spawn("env_explosion", kv))
{
    ops.AcceptInput(boom, "Explode");      // fire an input now
    ops.RemoveDelayed(boom, 1.0f);         // deferred "Kill" - the sanctioned cleanup
}

ops.EmitSound(entity, "SoundEventName");   // .vsndevts event name, not a file path
```

Cleanup rules: never `delete` an entity - use `Remove` (immediate) or `RemoveDelayed`
(preferred for short-lived effect helpers, runs through the engine's IO queue).
`NotifyFieldChanged(entity, "CClass", "m_field")` makes a direct schema `WriteAt` replicate
immediately instead of riding the next broadcast.

## EffectOps

One-shot world effects composed from EntityOps - free functions in `VoltMod::Sdk::EffectOps`
(`<VoltMod/Sdk/EffectOps.hpp>`). Each returns the helper entity (nullptr on failure) and cleans
itself up when a lifetime is given:

```cpp
using namespace VoltMod::Sdk;

EffectOps::SpawnParticle("particles/foo.vpcf", pos, 2.0f);          // needs Precache.Add for custom vpcf
EffectOps::SpawnBeam(from, to, Color(0, 128, 255, 255), 1.5f, 1.0f);
EffectOps::SpawnProp("models/props/crate.vmdl", pos, /*physics*/ true, 30.0f);
```

## PrecacheService

@ref VoltMod::Sdk::PrecacheService (`runtime.Precache`) registers a kit-owned game system that
receives `BuildGameSessionManifest`, letting plugins precache custom resources (particles, models,
sound events). Queue paths any time; they apply at the **next map load** - the engine's manifest
only exists inside that event. Assets that are not part of the map must also reach clients (e.g.
via a workshop addon), or they precache server-side but render nothing.

```cpp
runtime.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

Registered automatically by `VoltMod::Initialize` under a `LogPrefix`-derived name; detached safely
on unload (factory unlink + dispatcher/active-list removal), so `meta unload` mid-map is safe.
