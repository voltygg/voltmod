#pragma once

#include <cstdint>
#include <string>

namespace VoltMod
{

/**
 * The engine's convar value kinds, mirrored one-for-one from `EConVarType`.
 *
 * Mirrored rather than used directly so the two decisions that do not need a running engine - is
 * this convar's kind the C++ type the handle promises, and what text does a console line carry -
 * live in a translation unit that does not include the SDK. ConVars.cpp static_asserts that the
 * values still line up.
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
 * Whether a convar of engine kind @p type can be read and written as @p T.
 *
 * Deliberately per-kind, not per-width: every integer kind matches `int` (the handle then reads
 * and writes through the engine's own width), but `int` never matches a `bool` convar - which is
 * the silent no-op this check exists to catch.
 */
template <class T>
bool ConVarTypeMatches(ConVarType type);

}  // namespace VoltMod
