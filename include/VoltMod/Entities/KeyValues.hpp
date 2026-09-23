#pragma once

#include <VoltMod/Engine/Color.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Spawn keyvalues such as "origin", "spawnflags" or "effect_name".
 *
 * Owns the engine object until a spawn takes it; one that never spawns frees it.
 */
class KeyValues
{
public:
    KeyValues();
    ~KeyValues();
    KeyValues(const KeyValues&) = delete;
    KeyValues& operator=(const KeyValues&) = delete;

    KeyValues& Set(std::string_view key, std::string_view value);

    /** Without this a string literal picks the bool overload and sets "true". */
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

    /** Hand the object to the engine's spawn, which frees it. */
    CEntityKeyValues* Detach();

private:
    CEntityKeyValues* _kv;
};

}  // namespace VoltMod
