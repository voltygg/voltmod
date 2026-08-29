#include "Menu/MenuKeys.hpp"

#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <algorithm>

namespace VoltMod
{

MenuKeys::MenuKeys(MenuManager& menus, const MenuServices& services) : _menus(menus), _services(services) {}

bool MenuKeys::Handle(int slot, MenuDriver& driver)
{
    if (!_menus.Current(slot))
        return false;

    PlayerMenuState& state = _menus._states[slot];
    const uint64_t buttons = _services.Entities.Buttons(slot);
    const uint64_t pressed = buttons & ~state.PrevButtons;
    state.PrevButtons = buttons;

    if (pressed == 0)
        return false;

    const int64_t now = Time::MonotonicMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return false;

    // While a chat-input capture is active, the only key honoured is R (cancel) - every other
    // input is ignored so the menu does not drift while the player types in chat.
    if (_services.ChatInput.IsCapturing(slot))
    {
        if ((pressed & IN_RELOAD) == 0)
            return false;

        _services.ChatInput.CancelCapture(slot);
        state.LastInputTime = now;
        return true;
    }

    if (!Act(slot, driver, pressed))
        return false;

    // Written after the fact rather than up front: an input the menu had no use for should not
    // start a debounce window the next one has to wait out. Re-read, because acting on the key
    // may have replaced the session this started in.
    _menus._states[slot].LastInputTime = now;
    return true;
}

bool MenuKeys::Act(int slot, MenuDriver& driver, uint64_t pressed)
{
    // Before the row count: an empty menu still has to be closable.
    if (pressed & IN_RELOAD)
    {
        _menus.Close(slot);
        return true;
    }

    Menu* menu = _menus.Current(slot);
    const int items = menu ? static_cast<int>(menu->Items.size()) : 0;
    if (items == 0)
        return false;

    if (pressed & IN_FORWARD)
    {
        MoveCursor(slot, driver, -1);
        return true;
    }
    if (pressed & IN_BACK)
    {
        MoveCursor(slot, driver, +1);
        return true;
    }
    if (pressed & (IN_MOVELEFT | IN_MOVERIGHT))
    {
        const int direction = (pressed & IN_MOVELEFT) ? -1 : +1;
        if (_menus.Step(slot, _menus.Selected(slot), direction))
            return true;
        if (items <= driver.RowsPerPage())
            return false;
        JumpPage(slot, driver, direction);
        return true;
    }
    if (pressed & IN_USE)
    {
        _menus.Activate(slot, _menus.Selected(slot));
        return true;
    }
    return false;
}

void MenuKeys::MoveCursor(int slot, MenuDriver& driver, int step)
{
    Menu* menu = _menus.Current(slot);
    if (!menu)
        return;

    int index = _menus.Selected(slot);
    MenuManager::StepCursor(slot, menu->Items, index, step);
    _menus.Select(slot, index);
    driver.ShowPage(slot, index / driver.RowsPerPage());
}

void MenuKeys::JumpPage(int slot, MenuDriver& driver, int delta)
{
    Menu* menu = _menus.Current(slot);
    if (!menu || menu->Items.empty())
        return;

    // The offset within the page is preserved, then the cursor skips forward over rows it may
    // not land on within the new page.
    const int rows = driver.RowsPerPage();
    const int items = static_cast<int>(menu->Items.size());
    const int pages = PageCount(items, rows);
    const int index = _menus.Selected(slot);
    const int page = ((index / rows + delta) % pages + pages) % pages;

    const int start = page * rows;
    const int end = std::min(items, start + rows);

    int landed = std::min(start + index % rows, end - 1);
    for (int attempts = end - start; attempts > 0; --attempts)
    {
        if (MenuManager::IsCursorTarget(menu->Items[static_cast<std::size_t>(landed)], slot))
            break;
        landed = (landed + 1 < end) ? landed + 1 : start;
    }

    _menus.Select(slot, landed);
    driver.ShowPage(slot, page);
}

}  // namespace VoltMod
