#pragma once

#include <string_view>

namespace VoltMod
{

/** steam.inf's ServerVersion, or "unknown"; read once per process. */
std::string_view GameBuild();

}  // namespace VoltMod
