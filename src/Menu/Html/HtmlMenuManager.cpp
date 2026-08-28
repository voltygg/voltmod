#include "Menu/Html/MenuRenderer.hpp"

#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/Html/HtmlMenuManager.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <algorithm>

namespace VoltMod
{

// Preserves the in-page offset, then skips forward over disabled or non-selectable rows
// within the new page.
void HtmlMenuManager::JumpPage(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int pageDelta)
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
    while (!IsCursorTarget(items[idx]) && --attempts > 0)
    {
        idx = (idx + 1 < pageEnd) ? idx + 1 : pageStart;
    }
}

HtmlMenuManager::HtmlMenuManager(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities, Messages& messages,
                                 ChatInput& chatInput, Translations& translations, Policy& policy,
                                 PlayerManager& players)
    : MenuHost(slots, entities, chatInput, translations, policy, players),
      _messages(messages),
      _onFrame(scheduler.EveryFrame([this] { OnGameFrame(); }))
{}

void HtmlMenuManager::OnGameFrame()
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        auto& state = _states[slot];
        if (!state.HasMenu())
            continue;

        uint64_t buttons = _entities.Buttons(slot);
        auto prev = state.PrevButtons;
        state.PrevButtons = buttons;

        HandleInput(slot, buttons, prev);
        Present(slot);
    }
}

void HtmlMenuManager::HandleInput(int slot, uint64_t buttons, uint64_t prevButtons)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    uint64_t pressed = buttons & ~prevButtons;
    if (pressed == 0)
        return;

    auto now = Time::MonotonicMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return;

    // While a chat-input capture is active, the only key we honor is R (cancel) - every
    // other input is ignored so the menu doesn't drift while the player types in chat.
    if (_chatInput.IsCapturing(slot))
    {
        if (pressed & IN_RELOAD)
        {
            _chatInput.CancelCapture(slot);
            state.LastInputTime = now;
        }
        return;
    }

    int itemCount = static_cast<int>(menu->Items.size());
    if (itemCount == 0)
        return;

    bool isPaginated = itemCount > ItemsPerPage;
    bool inputHandled = true;

    if (pressed & IN_FORWARD)
        StepCursor(menu->Items, state.SelectedIndex, -1);
    else if (pressed & IN_BACK)
        StepCursor(menu->Items, state.SelectedIndex, +1);
    else if (pressed & IN_MOVELEFT)
    {
        bool consumed = Step(slot, state.SelectedIndex, -1);
        if (!consumed && isPaginated)
            JumpPage(menu->Items, state.SelectedIndex, -1);
        else if (!consumed)
            inputHandled = false;
    }
    else if (pressed & IN_MOVERIGHT)
    {
        bool consumed = Step(slot, state.SelectedIndex, +1);
        if (!consumed && isPaginated)
            JumpPage(menu->Items, state.SelectedIndex, +1);
        else if (!consumed)
            inputHandled = false;
    }
    else if (pressed & IN_USE)
        Activate(slot, state.SelectedIndex);
    else if (pressed & IN_RELOAD)
        CloseMenu(slot);
    else
        inputHandled = false;

    if (inputHandled)
        state.LastInputTime = now;
}

void HtmlMenuManager::Present(int slot)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    // While a capture is pending, render a prompt overlay instead of the item list.
    if (auto prompt = _chatInput.GetPrompt(slot))
    {
        _messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    bool isSubmenu = state.MenuStack.size() > 1;
    _messages.SendCenterHtml(slot, RenderMenuHtml(menu, slot, state.SelectedIndex, isSubmenu, _translations));
}

void HtmlMenuManager::Dismiss(int slot)
{
    _messages.ClearCenterHtml(slot);
}

}  // namespace VoltMod
