#pragma once

#include <cstdint>

namespace VoltMod
{

/** How a dead or spectating player's camera follows; Pawn.cpp checks the values against the SDK. */
enum class ObserverMode : uint8_t
{
    None = 0,
    Fixed = 1,
    InEye = 2,
    Chase = 3,
    Roaming = 4,
};

}  // namespace VoltMod
