#include "Menu/MenuRenderer.hpp"

#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Menu/MenuOption.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/PlayerController.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <algorithm>
#include <chrono>

namespace CS2Kit::Menu
{

using namespace CS2Kit::Core;
using namespace CS2Kit::Sdk;

static int64_t GetCurrentTimeMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

namespace
{

bool IsCursorTarget(const std::shared_ptr<MenuOption>& opt)
{
    return opt && opt->IsEnabled() && opt->IsSelectable();
}

// Step the cursor by `step` (typically ±1), wrapping over the full item list and skipping
// disabled or non-selectable rows (Text, ProgressBar).
void StepCursor(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int step)
{
    int n = static_cast<int>(items.size());
    if (n == 0)
        return;

    int attempts = n;
    do
    {
        idx = ((idx + step) % n + n) % n;
    }
    while (!IsCursorTarget(items[idx]) && --attempts > 0);
}

// Jump by `pageDelta` pages, preserving the in-page offset, then skip forward over disabled
// or non-selectable rows within the new page.
void JumpPage(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int pageDelta)
{
    int n = static_cast<int>(items.size());
    if (n == 0)
        return;

    int pageCount = (n + ItemsPerPage - 1) / ItemsPerPage;
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

}  // namespace

void MenuManager::OpenMenu(int slot, std::shared_ptr<MenuView> menu, MenuSessionOptions options)
{
    if (!Core::IsValidSlot(slot) || !menu)
        return;

    auto& state = _states[slot];
    // Options belong to the call that opens the stack; a submenu pushed onto a live session
    // inherits it, so an unfrozen session stays unfrozen throughout.
    if (state.MenuStack.empty() && options.FreezeMovement)
        SetPlayerFrozen(slot, true);

    state.MenuStack.push(std::move(menu));
    state.SelectedIndex = 0;
    state.LastInputTime = GetCurrentTimeMs();

    auto* current = state.GetCurrentMenu();
    if (current)
    {
        // Move cursor onto the first selectable row so disabled/Text/ProgressBar entries
        // are not greeted as the initial selection.
        if (!current->Items.empty() && !IsCursorTarget(current->Items[0]))
            StepCursor(current->Items, state.SelectedIndex, +1);

        Log::Info("Menu opened for slot {} (title: {}, items: {})", slot, current->Title, current->Items.size());
    }
}

void MenuManager::CloseMenu(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return;

    auto menu = state.MenuStack.top();
    state.MenuStack.pop();

    if (menu->OnClose)
        menu->OnClose(slot);

    if (state.MenuStack.empty())
    {
        SetPlayerFrozen(slot, false);
        CS2Kit::Detail::Rt().Messages.ClearCenterHtml(slot);
        state.Reset();
    }
    else
    {
        state.SelectedIndex = 0;
        if (auto* parent = state.GetCurrentMenu();
            parent && !parent->Items.empty() && !IsCursorTarget(parent->Items[0]))
        {
            StepCursor(parent->Items, state.SelectedIndex, +1);
        }
    }
}

void MenuManager::CloseAllMenus(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    SetPlayerFrozen(slot, false);
    state.Reset();
    CS2Kit::Detail::Rt().Messages.ClearCenterHtml(slot);
}

void MenuManager::SetPlayerFrozen(int slot, bool frozen)
{
    // Only the freeze direction is gated. Releasing must always run: gating both meant turning
    // the setting off while sessions were open stranded whoever was already frozen, with no
    // path back short of a reconnect.
    if (frozen && !_freezePlayer)
        return;

    auto& state = _states[slot];

    // Skip redundant transitions so a freeze isn't double-applied (which would capture
    // MOVETYPE_NONE as the "previous" type) and an unfreeze isn't run on a never-frozen slot.
    if (frozen == state.MovementFrozen)
        return;

    PlayerController pc(slot);
    if (!pc.IsValid())
        return;

    if (frozen)
        state.PrevMoveType = pc.GetMoveType();

    pc.SetMoveType(frozen ? MoveType::None : state.PrevMoveType);
    state.MovementFrozen = frozen;
}

void MenuManager::SetFreezePlayer(bool enabled)
{
    _freezePlayer = enabled;

    if (enabled)
        return;

    // Turning it off releases whoever the previous setting already froze; leaving them stuck
    // until they close a menu they may not know is open is not a defensible reading of "off".
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
    {
        if (_states[slot].MovementFrozen)
            SetPlayerFrozen(slot, false);
    }
}

bool MenuManager::HasActiveMenu(int slot) const
{
    if (!Core::IsValidSlot(slot))
        return false;

    return _states[slot].HasMenu();
}

void MenuManager::OnGameFrame()
{
    auto& entities = CS2Kit::Detail::Rt().Entities;
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
    {
        auto& state = _states[slot];
        if (!state.HasMenu())
            continue;

        uint64_t buttons = entities.GetPlayerButtons(slot);
        auto prev = state.PrevButtons;
        state.PrevButtons = buttons;

        HandleInput(slot, buttons, prev);
        RenderMenu(slot);
    }
}

void MenuManager::HandleInput(int slot, uint64_t buttons, uint64_t prevButtons)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    uint64_t pressed = buttons & ~prevButtons;
    if (pressed == 0)
        return;

    auto now = GetCurrentTimeMs();
    if (now - state.LastInputTime < InputDebounceMs)
        return;

    // While a chat-input capture is active, the only key we honor is R (cancel) - every
    // other input is ignored so the menu doesn't drift while the player types in chat.
    auto& capture = CS2Kit::Detail::Rt().ChatInput;
    if (capture.IsCapturing(slot))
    {
        if (pressed & IN_RELOAD)
        {
            capture.CancelCapture(slot);
            state.LastInputTime = now;
        }
        return;
    }

    int itemCount = static_cast<int>(menu->Items.size());
    if (itemCount == 0)
        return;

    bool isPaginated = itemCount > ItemsPerPage;
    bool inputHandled = true;

    auto& currentOption = menu->Items[state.SelectedIndex];

    if (pressed & IN_FORWARD)
        StepCursor(menu->Items, state.SelectedIndex, -1);
    else if (pressed & IN_BACK)
        StepCursor(menu->Items, state.SelectedIndex, +1);
    else if (pressed & IN_MOVELEFT)
    {
        bool consumed = currentOption && currentOption->IsEnabled() && currentOption->OnHorizontal(slot, -1);
        if (!consumed && isPaginated)
            JumpPage(menu->Items, state.SelectedIndex, -1);
        else if (!consumed)
            inputHandled = false;
    }
    else if (pressed & IN_MOVERIGHT)
    {
        bool consumed = currentOption && currentOption->IsEnabled() && currentOption->OnHorizontal(slot, +1);
        if (!consumed && isPaginated)
            JumpPage(menu->Items, state.SelectedIndex, +1);
        else if (!consumed)
            inputHandled = false;
    }
    else if (pressed & IN_USE)
    {
        if (currentOption && currentOption->IsEnabled() && currentOption->IsSelectable())
            currentOption->OnActivate(slot);
    }
    else if (pressed & IN_RELOAD)
        CloseMenu(slot);
    else
        inputHandled = false;

    if (inputHandled)
        state.LastInputTime = now;
}

void MenuManager::RenderMenu(int slot)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (!menu)
        return;

    auto& rt = CS2Kit::Detail::Rt();

    // While a capture is pending, render a prompt overlay instead of the item list.
    if (auto* prompt = rt.ChatInput.GetPrompt(slot); prompt != nullptr)
    {
        rt.Messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    bool isSubmenu = state.MenuStack.size() > 1;
    rt.Messages.SendCenterHtml(slot, RenderMenuHtml(menu, slot, state.SelectedIndex, isSubmenu));
}

void MenuManager::OnPlayerDisconnect(int slot)
{
    if (!Core::IsValidSlot(slot))
        return;

    _states[slot].Reset();
    CS2Kit::Detail::Rt().Translations.ClearPlayerLanguage(slot);
}

}  // namespace CS2Kit::Menu
