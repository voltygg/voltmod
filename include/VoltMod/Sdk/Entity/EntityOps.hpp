#pragma once

class CEntityInstance;
class IRecipientFilter;

namespace VoltMod::Sdk
{

class EntityKeyValues;

/**
 * @brief Entity mutation operations resolved from gamedata signatures.
 *
 * Covers the create/spawn/input/remove lifecycle plus sound emission and
 * replication notification - the plumbing behind spawned effects (explosions,
 * particles, beams, props). All methods are game-thread only. Every method
 * guards on its own resolved pointer and no-ops (or returns nullptr) when the
 * signature is missing, so callers can branch on CanSpawn() for fallbacks but
 * never need to.
 */
class EntityOpsService
{
public:
    /** Resolve all function pointers once; Warns per missing signature.
     *  Returns true when the spawn trio (create + spawn + input) resolved. */
    bool Initialize();

    /** True when CreateByName, DispatchSpawn, and AcceptInput are available. */
    bool CanSpawn() const;

    /** Create an entity by classname without spawning it. nullptr on failure. */
    CEntityInstance* CreateByName(const char* className);

    /** Spawn a created entity. kv may be nullptr; when given it is consumed
     *  (Detach) - the engine owns the keyvalues from this call on. */
    void DispatchSpawn(CEntityInstance* entity, EntityKeyValues* kv);

    /** CreateByName + DispatchSpawn convenience. nullptr on failure. */
    CEntityInstance* Spawn(const char* className, EntityKeyValues& kv);

    /** Fire an entity input (e.g. "Explode", "Kill", "Start"). */
    void AcceptInput(CEntityInstance* entity, const char* input, const char* param = nullptr,
                     CEntityInstance* activator = nullptr, CEntityInstance* caller = nullptr);

    /** Fire an entity input carrying a float parameter (e.g. "SetScale"). Some inputs read the
     *  numeric variant directly and ignore a string param, so this passes a FIELD_FLOAT32 value. */
    void AcceptInputFloat(CEntityInstance* entity, const char* input, float value, CEntityInstance* activator = nullptr,
                          CEntityInstance* caller = nullptr);

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

    /** Notify the engine that a schema field written via WriteAt changed, so the
     *  new value replicates immediately instead of riding the next broadcast. */
    void NotifyFieldChanged(CEntityInstance* entity, const char* className, const char* fieldName);

private:
    // Stored untyped so variant_t/CEntityKeyValues/EmitSound_t never leak into
    // this header; EntityOps.cpp bit_casts to file-local typedefs.
    void* _createEntityByName = nullptr;
    void* _dispatchSpawn = nullptr;
    void* _acceptInput = nullptr;
    void* _addEntityIOEvent = nullptr;
    void* _utilRemove = nullptr;
    void* _setModel = nullptr;
    void* _emitSoundParams = nullptr;
    void* _emitSoundFilter = nullptr;
};

}  // namespace VoltMod::Sdk
