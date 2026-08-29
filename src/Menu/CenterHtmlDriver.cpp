#include "Menu/CenterHtmlDriver.hpp"

#include "Menu/CenterHtmlRender.hpp"

#include <VoltMod/Core/Time.hpp>
#include <algorithm>

namespace VoltMod
{

CenterHtmlDriver::CenterHtmlDriver(MenuManager& menus, const MenuServices& services) : MenuDriver(menus, services)
{
    _states.BindReset(services.Slots);
}

// Preserves the in-page offset, then skips forward over disabled or non-selectable rows
// within the new page.
void CenterHtmlDriver::JumpPage(int slot, const std::vector<MenuItem>& items, int& idx, int pageDelta)
{
    int n = static_cast<int>(items.size());
    if (n == 0)
        return;

    int pageCount = PageCount(n, ItemsPerPage);
    int currentPage = idx / ItemsPerPage;
    int offset = idx % ItemsPerPage;
    int newPage = ((currentPage + pageDelta) % pageCount + pageCount) % pageCount;

    int pageStart = newPage * ItemsPerPage;
    int pageEnd = std::min(n, pageStart + ItemsPerPage);

    idx = std::min(pageStart + offset, pageEnd - 1);
    int attempts = pageEnd - pageStart;
    while (!IsCursorTarget(items[idx], slot) && --attempts > 0)
    {
        idx = (idx + 1 < pageEnd) ? idx + 1 : pageStart;
    }
}

void CenterHtmlDriver::Reset(int slot)
{
    auto& state = _states[slot];
    state.LastInputTime = Time::MonotonicMs();
    SelectFirst(slot, state.SelectedIndex);
}

bool CenterHtmlDriver::HandleInput(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return false;

    auto& state = _states[slot];
    const uint64_t buttons = _services.Entities.Buttons(slot);
    const uint64_t pressed = buttons & ~state.PrevButtons;
    state.PrevButtons = buttons;

    if (pressed == 0)
        return false;

    const int64_t now = Time::MonotonicMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return false;

    // While a chat-input capture is active, the only key we honor is R (cancel) - every
    // other input is ignored so the menu doesn't drift while the player types in chat.
    if (_services.ChatInput.IsCapturing(slot))
    {
        if (pressed & IN_RELOAD)
        {
            _services.ChatInput.CancelCapture(slot);
            state.LastInputTime = now;
            return true;
        }
        return false;
    }

    if (!HandlePressed(slot, *menu, pressed))
        return false;

    // Written after the fact rather than up front: an input the menu had no use for should not
    // start a debounce window the next one has to wait out.
    _states[slot].LastInputTime = now;
    return true;
}

bool CenterHtmlDriver::HandlePressed(int slot, const Menu& menu, uint64_t pressed)
{
    const int itemCount = static_cast<int>(menu.Items.size());
    if (itemCount == 0)
        return false;

    const bool isPaginated = itemCount > ItemsPerPage;
    int& selected = _states[slot].SelectedIndex;

    if (pressed & IN_FORWARD)
    {
        StepCursor(slot, menu.Items, selected, -1);
        return true;
    }
    if (pressed & IN_BACK)
    {
        StepCursor(slot, menu.Items, selected, +1);
        return true;
    }
    if (pressed & (IN_MOVELEFT | IN_MOVERIGHT))
    {
        const int direction = (pressed & IN_MOVELEFT) ? -1 : +1;
        if (Step(slot, selected, direction))
            return true;
        if (!isPaginated)
            return false;
        JumpPage(slot, menu.Items, selected, direction);
        return true;
    }
    if (pressed & IN_USE)
    {
        Activate(slot, selected);
        return true;
    }
    if (pressed & IN_RELOAD)
    {
        _menus.Close(slot);
        return true;
    }
    return false;
}

void CenterHtmlDriver::Present(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return;

    // While a capture is pending, render a prompt overlay instead of the item list.
    if (auto prompt = _services.ChatInput.GetPrompt(slot))
    {
        _services.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    const bool isSubmenu = Depth(slot) > 1;
    _services.Messages.SendCenterHtml(
        slot, RenderMenuHtml(menu, slot, _states[slot].SelectedIndex, isSubmenu, _services.Translations));
}

void CenterHtmlDriver::Dismiss(int slot)
{
    _services.Messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
