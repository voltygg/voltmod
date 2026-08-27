# Spawning and effects {#sdk_entity_ops_guide}

[TOC]

## EntityOps (spawning, entity IO, sound)

@ref VoltMod::EntityOps exposes entity spawning, inputs, deferred I/O, removal, models, and sound.
Build spawn data with @ref VoltMod::KeyValues; the engine consumes it during `DispatchSpawn`.
Unavailable operations safely do nothing. `CanSpawn()` checks the two bindings used by `Spawn()`.

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

For unwrapped entities, use a `static` @ref VoltMod::LazyField and notify live writes with
@ref VoltMod::MarkChanged:

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

## Precache

@ref VoltMod::Precache queues custom resources for the next map's session manifest. Assets outside
the map must also be delivered to clients, such as through a workshop addon.

```cpp
runtime.World.Precache.Add("particles/my_plugin/lightning_strike.vpcf");
```

`Runtime::Start()` registers the game system and unload detaches it safely.
