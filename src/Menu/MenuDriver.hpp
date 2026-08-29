#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief How a @ref MenuManager draws a menu and reads input for it.
 *
 * The manager owns the session - the stack, the freeze, the chat prompt - and calls into a driver
 * once per frame for each slot with a menu open: @ref HandleInput first, then @ref Present. The
 * cursor and the keys are the session's, shared by every driver; a driver keeps only what *it*
 * needs per slot - a page - and reaches the session through the protected helpers below, which
 * are the whole of what it may touch.
 *
 * Internal to `src/`: swapping drivers is @ref MenuManager::UsePanorama, not a type a consumer
 * names.
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
     *  page - starts over. The cursor and the debounce window are the session's, and the manager
     *  has already started those over, so a driver that keeps nothing per menu needs no override. */
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
    /** @p menus and @p services must outlive the driver, which the manager owning it gives. */
    MenuDriver(MenuManager& menus, const MenuServices& services) : _menus(menus), _services(services) {}

    /** The menu on top of @p slot's stack, or null when nothing is open. */
    [[nodiscard]] Menu* Current(int slot) const { return _menus.Current(slot); }

    /** How many menus are stacked for @p slot; more than one means a submenu, which is what a
     *  Back control is drawn for. */
    [[nodiscard]] int Depth(int slot) const { return _menus.Depth(slot); }

    /** Run row @p index of @p slot's current menu. Ignores rows the cursor could not land on. */
    void Activate(int slot, int index) { _menus.Activate(slot, index); }

    /** Nudge row @p index by @p direction; false means the row did not take it, which is what
     *  tells a keyboard driver to page instead. */
    bool Step(int slot, int index, int direction) { return _menus.Step(slot, index, direction); }

    /** Row @p index as it describes itself to @p slot, with @ref MenuRow::Pending and
     *  @ref MenuRow::Changed filled in. What a driver draws from, rather than calling
     *  @ref MenuItem::Describe itself. */
    [[nodiscard]] MenuRow Describe(int slot, int index) const { return _menus.Describe(slot, index); }

    /** Where @p slot's shared cursor is. */
    [[nodiscard]] int Selected(int slot) const { return _menus.Selected(slot); }

    /** Put @p slot's cursor on row @p index, applying what the row it leaves was holding. */
    void Select(int slot, int index) { _menus.Select(slot, index); }

    /** Put @p slot's cursor on the first row it may land on within @p page of this driver. */
    void SelectOnPage(int slot, int page) const { _menus.SelectOnPage(slot, page, RowsPerPage()); }

    /** The titles under the current menu, joined with ` > `; empty at the root. Valid until
     *  @p slot's stack moves, which outlasts any one draw. */
    [[nodiscard]] std::string_view Breadcrumb(int slot) const { return _menus.Breadcrumb(slot); }

    /** Whether keys drive @p slot's session (@ref MenuOptions::Keyboard). */
    [[nodiscard]] bool KeyboardEnabled(int slot) const { return _menus.KeyboardEnabled(slot); }

    /** The shared W/S/A/D/E/R handling, which is what a driver's @ref HandleInput calls. */
    bool HandleKeys(int slot) { return _menus.HandleKeys(slot, *this); }

    /** @p key in @p slot's language, or @p fallback when the table does not carry it - so a
     *  consumer that ships no `menu.*` keys still gets readable English rather than a dotted key
     *  on screen. */
    [[nodiscard]] std::string Translate(std::string_view key, std::string_view fallback, int slot) const
    {
        return _services.Translations.GetOr(key, slot, fallback);
    }

    MenuManager& _menus;
    const MenuServices& _services;
};

}  // namespace VoltMod
