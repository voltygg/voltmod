#include "Menu/MenuDriver.hpp"

#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>

namespace VoltMod
{

bool MenuDriver::HandleKeys(int slot)
{
    if (!_menus.Current(slot))
        return false;

    PlayerMenuState& state = _menus.State(slot);
    const uint64_t buttons = _services.Entities.Buttons(slot);
    const uint64_t pressed = buttons & ~state.PrevButtons;
    state.PrevButtons = buttons;

    if (pressed == 0)
        return false;

    const int64_t now = Time::MonotonicMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return false;

    if (_services.ChatInput.IsCapturing(slot))
    {
        if ((pressed & IN_RELOAD) == 0)
            return false;

        _services.ChatInput.CancelCapture(slot);
        state.LastInputTime = now;
        return true;
    }

    if (!HandlePressed(slot, pressed))
        return false;

    // The action may have replaced the session.
    _menus.State(slot).LastInputTime = now;
    return true;
}

bool MenuDriver::HandlePressed(int slot, uint64_t pressed)
{
    if (pressed & IN_RELOAD)
    {
        _session.Close(slot);
        return true;
    }

    Menu* menu = _menus.Current(slot);
    const int itemCount = menu ? static_cast<int>(menu->Items.size()) : 0;
    if (itemCount == 0)
        return false;

    if (pressed & IN_FORWARD)
    {
        MoveCursor(slot, -1);
        return true;
    }
    if (pressed & IN_BACK)
    {
        MoveCursor(slot, +1);
        return true;
    }
    if (pressed & (IN_MOVELEFT | IN_MOVERIGHT))
    {
        const int direction = (pressed & IN_MOVELEFT) ? -1 : +1;
        if (_menus.Step(slot, _menus.Selected(slot), direction))
            return true;
        if (itemCount <= RowsPerPage())
            return false;
        JumpPage(slot, direction);
        return true;
    }
    if (pressed & IN_USE)
    {
        _menus.Activate(slot, _menus.Selected(slot));
        return true;
    }
    return false;
}

void MenuDriver::MoveCursor(int slot, int step)
{
    if (!_menus.Current(slot))
        return;

    const int index = MenuCursor::Step(_menus.Rows(slot), _menus.Selected(slot), step);
    _menus.Select(slot, index);
    ShowPage(slot, index / RowsPerPage());
}

void MenuDriver::JumpPage(int slot, int delta)
{
    Menu* menu = _menus.Current(slot);
    if (!menu || menu->Items.empty())
        return;

    const int rows = RowsPerPage();
    const int index = MenuCursor::JumpPage(_menus.Rows(slot), _menus.Selected(slot), rows, delta);
    _menus.Select(slot, index);
    ShowPage(slot, index / rows);
}

}  // namespace VoltMod
