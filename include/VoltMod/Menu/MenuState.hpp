#pragma once

#include <VoltMod/Entities/MovementFreeze.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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

/** Internal state for one player's menu session. */
struct PlayerMenuState
{
    /** Open menus, innermost last. */
    std::vector<std::shared_ptr<Menu>> MenuStack;

    /** @ref MenuOptions::FreezeMovement for this session. */
    bool FreezeMovement = true;
    /** The pawn this session is holding still, if any. */
    MovementFreeze Freeze;

    /** Buttons held last frame, for edge detection. */
    uint64_t PrevButtons = 0;
    /** Monotonic milliseconds of the last key this session acted on. */
    int64_t LastInputTime = 0;
    int Selected = 0;

    /** One entry per row of the current menu, rebuilt when the menu changes. */
    std::vector<MenuRowMemory> Rows;

    /** Joined titles below the current menu. */
    std::string Breadcrumb;

    /** True if the player has any menu currently open. */
    bool HasMenu() const { return !MenuStack.empty(); }
    /** Top of the stack, or nullptr if no menu is open. */
    Menu* GetCurrentMenu() { return MenuStack.empty() ? nullptr : MenuStack.back().get(); }

    /** Clears the entire menu stack and the freeze bookkeeping. */
    void Reset() { *this = {}; }
};

}  // namespace VoltMod
