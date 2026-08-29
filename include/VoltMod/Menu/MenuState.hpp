#pragma once

#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/MoveType.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @brief Options for the session an @ref MenuManager::Open call starts.
 *
 * A session runs from the `Open` that starts it until the player's stack empties; a submenu pushed
 * through @ref MenuSession::Open inherits these.
 */
struct MenuOptions
{
    /** Whether the global movement freeze (@ref MenuManager::FreezeWhileOpen) applies. Pass false
     *  for menus players reach mid-round, where being held still is worse than stray movement. */
    bool FreezeMovement = true;

    /** Whether W/S/A/D/E/R drive this session. Center HTML ignores it - keys are the only input
     *  it has - so it turns the keyboard off for a Panorama session, where the player has a
     *  cursor and the rows are buttons. */
    bool Keyboard = true;
};

/** What a row last drew for a player, so the manager can tell a driver its value just moved. */
struct MenuRowMemory
{
    /** The last @ref MenuRow::Value the row described itself with. */
    std::string Value;
    /** Monotonic milliseconds of the last change; 0 until one happens. */
    int64_t ChangedAt = 0;
    /** False until the row has described itself once: arriving on screen is not a change. */
    bool Drawn = false;
};

/**
 * @brief One player's session: the stack of open menus, the keys read for it, and the freeze
 * taken out for it.
 *
 * The cursor over the stack is not here: it lives in the manager's own `MenuCursor`, which both
 * drivers and the shared key handler move through @ref MenuManager::Select.
 */
struct PlayerMenuState
{
    /** The stack of menus currently open for the player, innermost last. A vector rather than a
     *  `std::stack` because the titles underneath the top one are the breadcrumb. */
    std::vector<std::shared_ptr<Menu>> MenuStack;

    /** @ref MenuOptions::FreezeMovement for this session. */
    bool FreezeMovement = true;
    /** The pawn held frozen, or unset. Only this pawn is ever given @ref PrevMoveType back; a
     *  respawn gets a fresh freeze instead of a dead body's move type. */
    EntityRef FrozenPawn;
    /** MoveType captured before freezing, restored when the menu closes. */
    MoveType PrevMoveType = MoveType::Walk;
    /** @ref MenuOptions::Keyboard for this session. */
    bool Keyboard = true;
    /** Drawn as center HTML while the player is not alive (see @ref MenuManager). */
    bool OnFallback = false;

    /** Buttons held last frame, for edge detection. */
    uint64_t PrevButtons = 0;
    /** Monotonic milliseconds of the last key this session acted on. */
    int64_t LastInputTime = 0;

    /** One entry per row of the current menu, rebuilt when the menu changes. */
    std::vector<MenuRowMemory> Rows;

    /** The titles under the top menu, already joined. Built when the stack moves rather than on
     *  every draw: a driver asks for it once per frame and it only changes on a push or a pop. */
    std::string Breadcrumb;

    /** True if the player has any menu currently open. */
    bool HasMenu() const { return !MenuStack.empty(); }
    /** Top of the stack, or nullptr if no menu is open. */
    Menu* GetCurrentMenu() { return MenuStack.empty() ? nullptr : MenuStack.back().get(); }

    /** Clears the entire menu stack and the freeze bookkeeping. */
    void Reset() { *this = {}; }
};

}  // namespace VoltMod
