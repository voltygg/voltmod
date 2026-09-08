#pragma once

#include "Menu/MenuDriver.hpp"
#include "Menu/OpenMenus.hpp"

#include <VoltMod/Menu/MenuManager.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief W/S/A/D/E/R over an open menu, for whichever driver is drawing it.
 *
 * Called from both drivers' `HandleInput`, so the two share one cursor and one key table; the
 * cursor, the last-input time and the previous button mask live in @ref OpenMenus, so switching
 * drivers mid-session does not restart navigation. What differs is the shape of a page, which
 * arrives as @ref MenuDriver::RowsPerPage and @ref MenuDriver::ShowPage.
 *
 * Nothing here assumes the keys arrive: input capture may take them, and no key means no change.
 */
class MenuKeys
{
public:
    /** All three must outlive this. */
    MenuKeys(OpenMenus& menus, MenuSession& session, const MenuServices& services);

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

    OpenMenus& _menus;
    MenuSession& _session;
    const MenuServices& _services;
};

}  // namespace VoltMod
