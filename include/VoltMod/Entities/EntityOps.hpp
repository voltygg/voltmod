#pragma once

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/KeyValues.hpp>

namespace VoltMod
{

/**
 * @brief Entity mutation operations, driven by the entity function bindings.
 *
 * Covers the create/spawn/input/remove lifecycle plus sound emission and
 * replication notification - the plumbing behind spawned effects (explosions,
 * particles, beams, props). All methods are game-thread only. Every method
 * guards on the binding it uses and no-ops (or returns nullptr) when it
 * did not resolve, so callers can branch on CanSpawn() for fallbacks but
 * never need to.
 */
class EntityOps
{
public:
    /** Both must outlive this service; the Runtime declares them above it. */
    EntityOps(EntitySystem& entities, const Bindings& bindings) : _entities(entities), _bindings(bindings) {}
    EntityOps(const EntityOps&) = delete;
    EntityOps& operator=(const EntityOps&) = delete;

    /** True when CreateByName, DispatchSpawn, and AcceptInput all bound. */
    bool CanSpawn() const;

    /** Create an entity by classname without spawning it. nullptr on failure. */
    CEntityInstance* CreateByName(const char* className);

    /** Spawn a created entity. kv may be nullptr; when given it is consumed
     *  (Detach) - the engine owns the keyvalues from this call on. */
    void DispatchSpawn(CEntityInstance* entity, KeyValues* kv);

    /** CreateByName + DispatchSpawn convenience. nullptr on failure. */
    CEntityInstance* Spawn(const char* className, KeyValues& kv);

    /** Fire an entity input (e.g. "Explode", "Kill", "Start"). */
    void AcceptInput(CEntityInstance* entity, const char* input, const char* param = nullptr,
                     CEntityInstance* activator = nullptr, CEntityInstance* caller = nullptr);

    /** Fire an entity input carrying a float parameter (e.g. "SetScale"). Some inputs read the
     *  numeric variant directly and ignore a string param, so this passes a FIELD_FLOAT32 value. */
    void AcceptInputFloat(CEntityInstance* entity, const char* input, float value, CEntityInstance* activator = nullptr,
                          CEntityInstance* caller = nullptr);

    /** Scale a model entity (a player pawn, a prop) via the "SetScale" input, which updates both
     *  its render size and its collision hull. 1.0 is default; the value is clamped to a safe
     *  range so oversized scales cannot destabilize the server. */
    void SetModelScale(CEntityInstance* entity, float scale);

    /** Fire an entity input after a delay via the engine's entity IO queue. */
    void AddIOEvent(CEntityInstance* target, const char* input, float delaySeconds,
                    CEntityInstance* activator = nullptr, CEntityInstance* caller = nullptr);

    /** Remove an entity immediately (UTIL_Remove). Never delete entities directly. */
    void Remove(CEntityInstance* entity);

    /** Remove an entity after a delay (deferred "Kill" input) - the preferred
     *  cleanup for short-lived effect entities. */
    void RemoveDelayed(CEntityInstance* entity, float delaySeconds);

    /** Set the model of a CBaseModelEntity-derived entity (prop_*, ...). */
    void SetModel(CEntityInstance* entity, const char* modelPath);

    /** Emit a sound event from an entity, audible per normal attenuation.
     *  soundEvent is a .vsndevts event name, not a file path. */
    void EmitSound(CEntityInstance* entity, const char* soundEvent, int pitch = 100, float volume = 1.0f,
                   float delay = 0.0f);

    /** Emit a sound event from an entity to the filtered recipients only. */
    void EmitSoundFilter(IRecipientFilter& filter, CEntityInstance* source, const char* soundEvent, float volume = 1.0f,
                         int pitch = 100);

private:
    EntitySystem& _entities;
    const Bindings& _bindings;
};

}  // namespace VoltMod
