#pragma once

#include <VoltMod/Engine/GameData/GameDataLocation.hpp>
#include <functional>
#include <string_view>

namespace VoltMod
{

/**
 * Reads what gamedata holds for @p name, when @p sections admits the section holding it.
 *
 * Injected so @ref Bindings::Bind is unit-tested against a plain lambda; the app passes the host's lookup.
 */
using GameDataLookup = std::function<GameDataLocation(GameDataSection sections, std::string_view name)>;

}  // namespace VoltMod
