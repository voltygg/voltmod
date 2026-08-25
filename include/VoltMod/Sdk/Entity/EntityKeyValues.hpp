#pragma once

class CEntityKeyValues;
class Vector;
class QAngle;
class Color;

namespace VoltMod::Sdk
{

/**
 * @brief Builder for entity spawn keyvalues ("origin", "spawnflags", "effect_name", ...).
 *
 * Owns the underlying CEntityKeyValues until EntityOpsService::DispatchSpawn/Spawn
 * consumes it via Detach() - from that point the engine refcounts the object and
 * freeing it here would be a double-free. A builder that is never spawned releases
 * its keyvalues in the destructor.
 */
class EntityKeyValues
{
public:
    EntityKeyValues();
    ~EntityKeyValues();
    EntityKeyValues(const EntityKeyValues&) = delete;
    EntityKeyValues& operator=(const EntityKeyValues&) = delete;

    EntityKeyValues& Set(const char* key, const char* value);
    EntityKeyValues& Set(const char* key, int value);
    EntityKeyValues& Set(const char* key, float value);
    EntityKeyValues& Set(const char* key, bool value);
    EntityKeyValues& Set(const char* key, const Vector& value);
    EntityKeyValues& Set(const char* key, const QAngle& value);
    EntityKeyValues& Set(const char* key, const Color& value);

    /** The wrapped object; nullptr after Detach(). */
    CEntityKeyValues* Raw() const { return _kv; }

    /** Hand ownership to the caller and forget the pointer. Used by DispatchSpawn. */
    CEntityKeyValues* Detach();

private:
    CEntityKeyValues* _kv;
};

}  // namespace VoltMod::Sdk
