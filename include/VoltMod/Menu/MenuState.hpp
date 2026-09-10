#pragma once

#include <cstdint>
#include <string>

namespace VoltMod
{

/** Options for a menu session. Submenus inherit them. */
struct MenuOptions
{
    /** Whether the global movement freeze (@ref MenuManager::FreezeWhileOpen) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray movement. */
    bool FreezeMovement = true;
};

/** Previous row state used for change feedback. */
struct MenuRowMemory
{
    /** The last @ref MenuRow::Value the row described itself with. */
    std::string Value;
    /** Monotonic milliseconds of the last change; 0 until one happens. */
    int64_t ChangedAt = 0;
    /** False until the row has described itself once: arriving on screen is not a change. */
    bool Drawn = false;
};

}  // namespace VoltMod
