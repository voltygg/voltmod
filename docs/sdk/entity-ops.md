# Spawning and effects {#sdk_entity_ops_guide}

[TOC]

## EntityOps (spawning, entity IO, sound)

@ref VoltMod::EntityOps (`runtime.EntityOps`) exposes
signature-resolved entity operations: create/spawn, inputs, deferred I/O,
removal, models, and sound events. Build spawn keyvalues with
@ref VoltMod::KeyValues; the engine consumes them during spawn, so do
not reuse or free them after `DispatchSpawn`. Methods no-op when their gamedata
signature is unavailable; use `CanSpawn()` when the plugin needs an explicit
fallback.

```cpp
auto& ops = runtime.EntityOps;

VoltMod::KeyValues kv;
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

A spawned entity has no wrapper of its own, so reach its schema fields through a `static`
@ref VoltMod::LazyField and dirty the write with @ref VoltMod::MarkChanged - which is what a
@ref VoltMod::Field does for you on a @ref VoltMod::Pawn or @ref VoltMod::Controller:

```cpp
static const VoltMod::LazyField kWidth{"CBeam", "m_fWidth", sizeof(float)};

if (kWidth)
{
    VoltMod::WriteAt<float>(beam, kWidth->Offset, 2.0f);
    VoltMod::MarkChanged(beam, *kWidth);   // unnecessary before DispatchSpawn
}
```

Fields written before `DispatchSpawn` go out with the first snapshot on their own, so the
notification is only needed for a live entity.

## EffectOps

One-shot world effects composed from EntityOps, as free functions in `VoltMod::EffectOps`
(`<VoltMod/Entities/EffectOps.hpp>`). Each takes the service it spawns through as its first
argument, returns the helper entity (nullptr on failure), and cleans itself up when a lifetime is
given:

```cpp
namespace EffectOps = VoltMod::EffectOps;
auto& ops = runtime.EntityOps;

EffectOps::SpawnParticle(ops, "particles/foo.vpcf", pos, 2.0f);          // needs Precache.Add for custom vpcf
EffectOps::SpawnBeam(ops, from, to, Color(0, 128, 255, 255), 1.5f, 1.0f);
EffectOps::SpawnProp(ops, "models/props/crate.vmdl", pos, /*physics*/ true, 30.0f);
```

## Precache

@ref VoltMod::Precache (`runtime.Precache`) registers a framework-owned game system that
receives `BuildGameSessionManifest`, so plugins can precache custom resources (particles, models,
sound events). Queue paths any time; they apply at the **next map load**, because the engine's
manifest only exists inside that event. Assets that are not part of the map must also reach clients (e.g.
via a workshop addon), or they precache server-side but render nothing.

```cpp
runtime.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

Registered by `Runtime::Start()` under a `LogPrefix`-derived name; detached safely
on unload (factory unlink + dispatcher/active-list removal), so `meta unload` mid-map is safe.
