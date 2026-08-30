# Spawning and effects {#sdk_entity_ops_guide}

[TOC]

## EntityOps (spawning, entity IO, sound)

@ref VoltMod::EntityOps handles entity spawning, inputs, removal, models, and
sound. Build spawn data with @ref VoltMod::KeyValues. Unavailable operations
fail safely, and `CanSpawn()` checks the bindings required by `Spawn()`.

```cpp
auto& ops = runtime.World.EntityOps;

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

For an entity with no curated wrapper, add its class to `schema/manifest.json`, regenerate, and
use the generated view. The setter dirties the field itself:

```cpp
VoltMod::Schema::CBeam{beam}.SetWidth(2.0f);
```

Fields written before `DispatchSpawn` go out with the first snapshot on their own, so the
notification a live entity needs costs nothing here.

## Precache

@ref VoltMod::Precache queues custom resources for the next map's session manifest. Assets outside
the map must also be delivered to clients, such as through a workshop addon.

```cpp
runtime.World.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

`Runtime::Start()` registers the game system and unload detaches it safely.
