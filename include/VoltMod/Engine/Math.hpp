#pragma once

// Include this for Vector and QAngle instead of the SDK's <mathlib/vector.h>.
#include <mathlib/vector.h>

namespace VoltMod
{

/** The engine's Vector. Its default constructor leaves the value uninitialised; write
 *  `Vector{0.0f, 0.0f, 0.0f}` for a zero. */
using Vector = ::Vector;

/** The engine's angles in degrees: (pitch, yaw, roll), positive pitch looks down. */
using QAngle = ::QAngle;

}  // namespace VoltMod
