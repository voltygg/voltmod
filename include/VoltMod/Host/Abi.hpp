#pragma once

#include <cstdint>

namespace VoltMod
{

/** Bump with any change under `VoltMod/Host/`. The host refuses a plugin built against another value. */
inline constexpr uint32_t HostAbiVersion = 5;

}  // namespace VoltMod
