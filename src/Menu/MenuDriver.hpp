#pragma once

#include "Menu/ActiveMenus.hpp"

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <cstdint>

namespace VoltMod
{

/** Internal interface for drawing a menu and reading its input. */
class MenuDriver
{
public:
    virtual ~MenuDriver() = default;

    MenuDriver(const MenuDriver&) = delete;
    MenuDriver& operator=(const MenuDriver&) = delete;

    /** Draw the menu at the top of @p slot's stack. False means this driver cannot show it for
     *  that player right now, which is the manager's cue to fall back to center HTML. */
    virtual bool Present(int slot) = 0;

    /** Remove this driver's UI for @p slot. */
    virtual void Dismiss(int slot) = 0;

    /** Reset state belonging to the menu at the top of @p slot's stack. */
    virtual void Reset(int /*slot*/) {}

    /** Read keyboard input. Panorama disables this for click-only sessions. */
    virtual bool HandleInput(int slot) { return HandleKeys(slot); }

    [[nodiscard]] virtual int RowsPerPage() const = 0;

    virtual void ShowPage(int /*slot*/, int /*page*/) {}

protected:
    /** Referenced objects must outlive the driver. */
    MenuDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services)
        : _menus(menus), _session(session), _services(services)
    {}

    /** Handle the shared W/S/A/D/E/R controls. */
    bool HandleKeys(int slot);

    ActiveMenus& _menus;
    MenuSession& _session;
    const MenuServices& _services;

private:
    static constexpr int64_t InputDebounceMs = 200;

    bool HandlePressed(int slot, uint64_t pressed);
    void MoveCursor(int slot, int step);
    void JumpPage(int slot, int delta);
};

}  // namespace VoltMod
