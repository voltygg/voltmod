#pragma once

class CEntityInstance;
class Vector;
class Color;

namespace VoltMod::Entities
{
class EntityOps;
}

/**
 * @file EffectOps.hpp
 * @brief One-shot world effects composed from EntityOps (PawnOps-style free
 * functions). Each spawns a helper entity, optionally schedules its own cleanup via
 * the entity IO queue, and returns the entity (nullptr when entity ops are
 * unavailable or the spawn failed).
 *
 * Every function takes the service it spawns through as its first parameter; pass
 * `runtime.EntityOps`.
 */
namespace VoltMod::Entities::EffectOps
{

/**
 * Spawn a particle system at @p origin (info_particle_system, started immediately).
 * Effects that are not baked into the map must be precached via
 * runtime.Precache.Add(effectName) and shipped to clients (workshop addon).
 * @param lifetimeSeconds destroy the helper entity after this long (> 0).
 */
CEntityInstance* SpawnParticle(EntityOps& ops, const char* effectName, const Vector& origin, float lifetimeSeconds);

/**
 * Spawn a solid beam from @p from to @p to using stock beam rendering.
 * @param width beam thickness in units (e.g. 1-3).
 * @param lifetimeSeconds destroy the beam after this long (> 0).
 */
CEntityInstance* SpawnBeam(EntityOps& ops, const Vector& from, const Vector& to, const Color& color, float width,
                           float lifetimeSeconds);

/**
 * Spawn a prop with @p modelPath at @p origin (prop_physics_override or
 * prop_dynamic_override). Models that are not baked into the map must be precached
 * via runtime.Precache.Add(modelPath) and shipped to clients.
 * @param lifetimeSeconds destroy the prop after this long; 0 keeps it until round end.
 */
CEntityInstance* SpawnProp(EntityOps& ops, const char* modelPath, const Vector& origin, bool physics,
                           float lifetimeSeconds = 0.0f);

}  // namespace VoltMod::Entities::EffectOps
