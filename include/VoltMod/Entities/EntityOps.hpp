#pragma once

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/KeyValues.hpp>

namespace VoltMod
{

/**
 * Bound entity creation, mutation, removal, and sound operations.
 * Methods run on the game thread and safely no-op when their binding is unavailable.
 */
class EntityOps
{
public:
    /** Both dependencies must outlive this service. */
    EntityOps(EntitySystem& entities, const Bindings& bindings) : _entities(entities), _bindings(bindings) {}
    EntityOps(const EntityOps&) = delete;
    EntityOps& operator=(const EntityOps&) = delete;

    /** Whether @ref Spawn has both required bindings. */
    bool CanSpawn() const;

    /** Create without spawning. Returns nullptr on failure. */
    CEntityInstance* CreateByName(const char* className);

    /** Spawn an entity. The engine takes ownership of non-null @p kv. */
    void DispatchSpawn(CEntityInstance* entity, KeyValues* kv);

    /** Create and spawn an entity. Returns nullptr on failure. */
    CEntityInstance* Spawn(const char* className, KeyValues& kv);

    /** Fire an entity input with an optional string value. */
    void AcceptInput(CEntityInstance* entity, const char* input, const char* param = nullptr,
                     CEntityInstance* activator = nullptr, CEntityInstance* caller = nullptr);

    /** Fire an entity input with a FIELD_FLOAT32 value. */
    void AcceptInputFloat(CEntityInstance* entity, const char* input, float value, CEntityInstance* activator = nullptr,
                          CEntityInstance* caller = nullptr);

    /** Set render and collision scale through `SetScale`, clamped to a safe range. */
    void SetModelScale(CEntityInstance* entity, float scale);

    /** Fire an entity input after a delay via the engine's entity IO queue. */
    void AddIOEvent(CEntityInstance* target, const char* input, float delaySeconds,
                    CEntityInstance* activator = nullptr, CEntityInstance* caller = nullptr);

    /** Remove immediately through UTIL_Remove. */
    void Remove(CEntityInstance* entity);

    /** Remove later through a deferred `Kill` input. */
    void RemoveDelayed(CEntityInstance* entity, float delaySeconds);

    /** Set a CBaseModelEntity model. */
    void SetModel(CEntityInstance* entity, const char* modelPath);

    /** Emit a `.vsndevts` event with normal attenuation. */
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
