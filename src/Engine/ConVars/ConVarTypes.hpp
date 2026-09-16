#pragma once

#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * SDK-free mirror of `EConVarType` used by handle validation and console parsing.
 * ConVar.cpp verifies every value against the SDK with static_asserts.
 */
enum class ConVarType : int16_t
{
    Invalid = -1,
    Bool,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float32,
    Float64,
    String,
    Color,
    Vector2,
    Vector3,
    Vector4,
    QAngle,
    VectorWS
};

/**
 * Whether a handle for @p T can access engine kind @p type.
 *
 * All integer kinds use `int`; the engine accessor preserves their storage width. Bool remains a
 * separate kind because accepting an `int` handle for it silently drops writes.
 */
template <class T>
bool ConVarTypeMatches(ConVarType type);

}  // namespace VoltMod
