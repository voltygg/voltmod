#pragma once

#include <VoltMod/Engine/GameData/GameDataLocation.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The gamedata the host resolved once for the process, before any plugin loaded.
 *
 * Game thread only. The entries never change after startup.
 */
struct IHostGameData
{
    /** What the host resolved for @p name, when @p sections admits the section holding it. */
    virtual GameDataLocation Lookup(GameDataSection sections, std::string_view name) = 0;

protected:
    ~IHostGameData() = default;
};

}  // namespace VoltMod
