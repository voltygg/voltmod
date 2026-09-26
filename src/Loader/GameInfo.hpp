#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The `Game` values under `SearchPaths` in a `gameinfo.gi`, in file order.
 *
 * Other keys (`Game_LowViolence`, `Mod`...), comments, quotes and trailing conditions are dropped.
 * The values are relative to the `game` directory.
 */
std::vector<std::string> GameSearchPaths(std::string_view gameinfo);

}  // namespace VoltMod
