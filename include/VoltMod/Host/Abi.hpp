#pragma once

#include <cstdint>

namespace VoltMod
{

/**
 * @brief The version of every interface in `VoltMod/Host/`, taken together.
 *
 * Any change to any of them bumps this in the same commit. The host refuses a plugin whose
 * descriptor names another value: host and plugins always ship from the same build, so there is
 * no older shape to stay compatible with.
 */
inline constexpr uint32_t HostAbiVersion = 2;

}  // namespace VoltMod
