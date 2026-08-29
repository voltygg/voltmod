#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <vector>

namespace VoltMod
{

/**
 * @brief How a @ref MenuManager draws a menu and reads input for it.
 *
 * The manager owns the session - the stack, the freeze, the chat prompt - and calls into a driver
 * once per frame for each slot with a menu open: @ref HandleInput first, then @ref Present. A
 * driver keeps only what *it* needs per slot (a cursor, a page), and reaches the session through
 * the protected helpers below, which are the whole of what it may touch.
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
     *  cursor, a page - starts over. */
    virtual void Reset(int slot) = 0;

    /** Read whatever input this driver has for @p slot. True when something was consumed; a
     *  driver whose input arrives as events rather than button state returns false. */
    virtual bool HandleInput(int slot) = 0;

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

    /** True when the cursor is allowed to land on @p item, as it describes itself to @p slot. */
    static bool IsCursorTarget(const MenuItem& item, int slot) { return MenuManager::IsCursorTarget(item, slot); }

    /** Move @p idx by @p step, wrapping over @p items and skipping rows the cursor cannot land on. */
    static void StepCursor(int slot, const std::vector<MenuItem>& items, int& idx, int step)
    {
        MenuManager::StepCursor(slot, items, idx, step);
    }

    /** Put @p index on the first row the cursor may land on. */
    void SelectFirst(int slot, int& index) const { _menus.SelectFirst(slot, index); }

    MenuManager& _menus;
    const MenuServices& _services;
};

}  // namespace VoltMod
