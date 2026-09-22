#pragma once

#include <VoltMod/Engine/Math.hpp>

namespace VoltMod
{

/** Unit vector pointing along @p angles, the way the engine aims: positive pitch looks down and
 *  roll does not change the direction. */
Vector AngleToForward(const QAngle& angles);

}  // namespace VoltMod
