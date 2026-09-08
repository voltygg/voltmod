#pragma once

#include "Menu/OpenMenus.hpp"

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief How a @ref MenuManager draws a menu and reads input for it.
 *
 * The manager calls into a driver once per frame for each slot with a menu open:
 * @ref HandleInput first, then @ref Present. What a driver asks about the menu it is drawing comes
 * from the shared @ref OpenMenus, which the keys move too; a driver keeps only its page.
 *
 * Internal to `src/`: swapping drivers is @ref MenuManager::UsePanorama.
 */
class MenuDriver
{
public:
    virtual ~MenuDriver() = default;

    MenuDriver(const MenuDriver&) = delete;
    MenuDriver& operator=(const MenuDriver&) = delete;

    /** Draw @p slot's current menu. */
    virtual void Present(int slot) = 0;

    /** @p slot has no menu any more: take whatever this driver put on their screen off it. */
    virtual void Dismiss(int slot) = 0;

    /** The menu on top of @p slot's stack changed, so anything this driver tracks per menu - a
     *  page - starts over. A driver that keeps nothing per menu needs no override. */
    virtual void Reset(int /*slot*/) {}

    /** Read whatever input this driver has for @p slot. True when something was consumed; a
     *  driver whose input arrives as events rather than button state returns false. */
    virtual bool HandleInput(int slot) = 0;

    /** Rows one page of this driver holds, which is what the shared key handler pages by. */
    [[nodiscard]] virtual int RowsPerPage() const = 0;

    /** The cursor moved onto @p page. A driver that keeps a page of its own follows it here; one
     *  that derives the page from the cursor has nothing to do. */
    virtual void ShowPage(int /*slot*/, int /*page*/) {}

protected:
    /** All four must outlive the driver. */
    MenuDriver(OpenMenus& menus, MenuKeys& keys, MenuSession& session, const MenuServices& services)
        : _menus(menus), _keys(keys), _session(session), _services(services)
    {}

    /** The shared W/S/A/D/E/R handling, which is what a driver's @ref HandleInput calls. Out of
     *  line so this header need not reach the key table. */
    bool HandleKeys(int slot);

    /** Put @p slot's cursor on the first row it may land on within @p page of this driver. */
    void SelectOnPage(int slot, int page) { _menus.SelectOnPage(slot, page, RowsPerPage()); }

    /** @p key in @p slot's language, or @p fallback when the table does not carry it. */
    [[nodiscard]] std::string Translate(std::string_view key, std::string_view fallback, int slot) const
    {
        return _session.Translate(slot, key, fallback);
    }

    OpenMenus& _menus;
    MenuKeys& _keys;
    MenuSession& _session;
    const MenuServices& _services;
};

}  // namespace VoltMod
