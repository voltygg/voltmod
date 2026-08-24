# Spawning and effects {#sdk_entity_ops_guide}

[TOC]

## EntityOps (spawning, entity IO, sound)

@ref VoltMod::Sdk::EntityOpsService (`runtime.EntityOps`) exposes
signature-resolved entity operations: create/spawn, inputs, deferred I/O,
removal, models, and sound events. Build spawn keyvalues with
@ref VoltMod::Sdk::EntityKeyValues; the engine consumes them during spawn, so do
not reuse or free them after `DispatchSpawn`. Methods no-op when their gamedata
signature is unavailable; use `CanSpawn()` when the plugin needs an explicit
fallback.

```cpp
auto& ops = runtime.EntityOps;

VoltMod::EntityKeyValues kv;
kv.Set("origin", pos).Set("spawnflags", 1);
if (auto* boom = ops.Spawn("env_explosion", kv))
{
    ops.AcceptInput(boom, "Explode");      // fire an input now
    ops.RemoveDelayed(boom, 1.0f);         // deferred "Kill", the sanctioned cleanup
}

ops.EmitSound(entity, "SoundEventName");   // .vsndevts event name, not a file path
```

Never `delete` an entity. Use `Remove` immediately or `RemoveDelayed` through
the engine's I/O queue.
`NotifyFieldChanged(entity, "CClass", "m_field")` makes a direct schema `WriteAt` replicate
immediately instead of riding the next broadcast.

## EffectOps

One-shot world effects composed from EntityOps, as free functions in `VoltMod::Sdk::EffectOps`
(`<VoltMod/Sdk/EffectOps.hpp>`). Each returns the helper entity (nullptr on failure) and cleans
itself up when a lifetime is given:

```cpp
using namespace VoltMod::Sdk;

EffectOps::SpawnParticle("particles/foo.vpcf", pos, 2.0f);          // needs Precache.Add for custom vpcf
EffectOps::SpawnBeam(from, to, Color(0, 128, 255, 255), 1.5f, 1.0f);
EffectOps::SpawnProp("models/props/crate.vmdl", pos, /*physics*/ true, 30.0f);
```

## PrecacheService

@ref VoltMod::Sdk::PrecacheService (`runtime.Precache`) registers a framework-owned game system that
receives `BuildGameSessionManifest`, so plugins can precache custom resources (particles, models,
sound events). Queue paths any time; they apply at the **next map load**, because the engine's
manifest only exists inside that event. Assets that are not part of the map must also reach clients (e.g.
via a workshop addon), or they precache server-side but render nothing.

```cpp
runtime.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

Registered by `Runtime::Start()` under a `LogPrefix`-derived name; detached safely
on unload (factory unlink + dispatcher/active-list removal), so `meta unload` mid-map is safe.
