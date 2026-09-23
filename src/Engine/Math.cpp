#include <VoltMod/Engine/Math.hpp>
#include <mathlib/mathlib.h>

namespace VoltMod
{

Vector AngleToForward(const QAngle& angles)
{
    Vector forward;
    AngleVectors(angles, &forward);
    return forward;
}

}  // namespace VoltMod
