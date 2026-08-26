#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief CPlayer_ObserverServices::m_iObserverMode values.
 * Mirrors the CS2 schema enum ObserverMode_t.
 */
enum class ObserverMode_t : uint8_t
{
    None = 0,
    Fixed = 1,
    InEye = 2,
    Chase = 3,
    Roaming = 4,
};

}  // namespace VoltMod
