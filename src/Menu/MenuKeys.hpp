#pragma once

#include "Menu/MenuDriver.hpp"

#include <VoltMod/Menu/MenuManager.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief W/S/A/D/E/R over an open menu, for whichever driver is drawing it.
 *
 * Held by @ref MenuManager and called from both drivers' `HandleInput`, so the two share one
 * cursor and one key table rather than the center-HTML driver owning the only keyboard. The
 * cursor, the last-input time and the previous button mask live in the manager's per-player
 * state for the same reason: switching drivers mid-session must not restart navigation.
 *
 * What differs between drivers is the shape of a page, which arrives as
 * @ref MenuDriver::RowsPerPage and @ref MenuDriver::ShowPage - the cursor moves, and a driver
 * that keeps a page of its own follows it.
 *
 * Nothing here assumes the keys arrive: a player holding a Panorama cursor may have them taken
 * by input capture, and no key simply means no change.
 */
class MenuKeys
{
public:
    /** @p menus and @p services must outlive this, which the manager owning it gives. */
    MenuKeys(MenuManager& menus, const MenuServices& services);

    /** Read @p slot's buttons and act on them for @p driver's page shape. True when a key was
     *  consumed, which is what stops a held key racing through the menu. */
    bool Handle(int slot, MenuDriver& driver);

private:
    /** Long enough that a held key steps once per beat rather than scrolling a menu away. */
    static constexpr int64_t InputDebounceMs = 200;

    /** Act on the keys that went down this frame. True when one of them was used. */
    bool Act(int slot, MenuDriver& driver, uint64_t pressed);

    /** Move the cursor by @p step over the rows the cursor may land on. */
    void MoveCursor(int slot, MenuDriver& driver, int step);

    /** Move the cursor a page of @p driver's height, keeping its offset within the page. */
    void JumpPage(int slot, MenuDriver& driver, int delta);

    MenuManager& _menus;
    const MenuServices& _services;
};

}  // namespace VoltMod
