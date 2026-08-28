#pragma once

#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <stack>
#include <string>
#include <vector>

namespace VoltMod
{

/** Maximum items shown per page before the menu paginates. */
inline constexpr int ItemsPerPage = 5;

/** A WASD-navigable menu rendered as center-HTML. Build with MenuBuilder. */
struct MenuView
{
    std::string Title;
    std::vector<std::shared_ptr<MenuOption>> Items;
};

/**
 * Per-player menu runtime state held by MenuHost. The stack supports
 * submenus: opening pushes, R or programmatic close pops back to the parent.
 */
struct PlayerMenuState
{
    /** The stack of menus currently open for the player. */
    std::stack<std::shared_ptr<MenuView>> MenuStack;
    int SelectedIndex = 0;
    int64_t LastInputTime = 0;
    uint64_t PrevButtons = 0;

    /** True while MenuHost is holding the player's movement frozen for this menu session. */
    bool MovementFrozen = false;
    /** MoveType captured before freezing, restored when the menu closes. */
    MoveType PrevMoveType = MoveType::Walk;

    /** True if the player has any menu currently open. */
    bool HasMenu() const { return !MenuStack.empty(); }
    /** Top of the stack, or nullptr if no menu is open. */
    MenuView* GetCurrentMenu() { return MenuStack.empty() ? nullptr : MenuStack.top().get(); }

    /** Clears the entire menu stack and resets selection/input state. */
    void Reset()
    {
        while (!MenuStack.empty())
        {
            MenuStack.pop();
        }

        SelectedIndex = 0;
        LastInputTime = 0;
        PrevButtons = 0;
        MovementFrozen = false;
        PrevMoveType = MoveType::Walk;
    }
};

}  // namespace VoltMod
