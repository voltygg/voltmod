#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief An RGBA color in the engine's byte order, as `m_clrRender` and glow colors hold it.
 *
 * A channel left out stays 255, so `Color{}` is opaque white, `Color{255, 0, 0}` is opaque red
 * and `Color{.A = 0}` is invisible.
 */
struct Color
{
    uint8_t R = 255;
    uint8_t G = 255;
    uint8_t B = 255;
    uint8_t A = 255;

    bool operator==(const Color&) const = default;
};

// Generated accessors read and write it in place of the engine's 4-byte Color.
static_assert(sizeof(Color) == 4);

}  // namespace VoltMod
