#pragma once

#include <cmath>
#include <mathlib/vector.h>
#include <numbers>

namespace VoltMod
{

/** Unit vector pointing along @p angles, the way the engine aims: positive pitch looks down and
 *  roll does not change the direction. */
inline Vector AngleToForward(const QAngle& angles)
{
    constexpr float radiansPerDegree = std::numbers::pi_v<float> / 180.0f;
    const float pitch = angles.x * radiansPerDegree;
    const float yaw = angles.y * radiansPerDegree;
    return Vector(std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch));
}

}  // namespace VoltMod
