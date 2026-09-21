#pragma once

#include <cstdint>

namespace VoltMod
{

/** CPlayer_ObserverServices::m_iObserverMode values; Entity.cpp checks them against the SDK's OBS_MODE_*. */
enum class ObserverMode : uint8_t
{
    None = 0,
    Fixed = 1,
    InEye = 2,
    Chase = 3,
    Roaming = 4,
};

}  // namespace VoltMod
