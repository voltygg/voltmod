#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Builder for entity spawn keyvalues ("origin", "spawnflags", "effect_name", ...).
 *
 * Owns the underlying CEntityKeyValues until EntityOps::DispatchSpawn/Spawn
 * consumes it via Detach() - from that point the engine refcounts the object and
 * freeing it here would be a double-free. A builder that is never spawned releases
 * its keyvalues in the destructor.
 */
class KeyValues
{
public:
    KeyValues();
    ~KeyValues();
    KeyValues(const KeyValues&) = delete;
    KeyValues& operator=(const KeyValues&) = delete;

    KeyValues& Set(std::string_view key, std::string_view value);

    /**
     * String literals and C strings.
     *
     * Without this they set the key to `"true"`: a pointer converts to `bool` by a standard
     * conversion and to `std::string_view` only by a user-defined one, so the bool overload wins
     * and the engine is handed a bool where a string_t was meant.
     */
    KeyValues& Set(std::string_view key, const char* value)
    {
        return Set(key, value ? std::string_view(value) : std::string_view{});
    }

    KeyValues& Set(std::string_view key, int value);
    KeyValues& Set(std::string_view key, float value);
    KeyValues& Set(std::string_view key, bool value);
    KeyValues& Set(std::string_view key, const Vector& value);
    KeyValues& Set(std::string_view key, const QAngle& value);
    KeyValues& Set(std::string_view key, const Color& value);

    /** The wrapped object; nullptr after Detach(). */
    CEntityKeyValues* Raw() const { return _kv; }

    /** Hand ownership to the caller and forget the pointer. Used by DispatchSpawn. */
    CEntityKeyValues* Detach();

private:
    CEntityKeyValues* _kv;
};

}  // namespace VoltMod
