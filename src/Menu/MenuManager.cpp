#include "Menu/MenuRenderer.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <algorithm>

namespace VoltMod
{

static bool IsCursorTarget(const std::shared_ptr<MenuOption>& opt)
{
    return opt && opt->IsEnabled() && opt->IsSelectable();
}

// Step the cursor by `step` (typically ±1), wrapping over the full item list and skipping
// disabled or non-selectable rows (Text rows, and anything a plugin marks unselectable).
static void StepCursor(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int step)
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
static void JumpPage(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int pageDelta)
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

MenuManager::MenuManager(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities, Messages& messages,
                         ChatInput& chatInput, Translations& translations, Policy& policy, PlayerManager& players)
    : _entities(entities),
      _messages(messages),
      _chatInput(chatInput),
      _translations(translations),
      _policy(policy),
      _players(players),
      _actions(policy, players, entities),
      _pump(scheduler.EveryFrame([this] { OnGameFrame(); }))
{
    _states.BindReset(slots);
}

void MenuManager::OpenMenu(int slot, std::shared_ptr<MenuView> menu, MenuSessionOptions options)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    auto& state = _states[slot];
    // Options belong to the call that opens the stack; a submenu pushed onto a live session
    // inherits it, so an unfrozen session stays unfrozen throughout.
    if (state.MenuStack.empty() && options.FreezeMovement)
        SetPlayerFrozen(slot, true);

    state.MenuStack.push(std::move(menu));
    state.SelectedIndex = 0;
    state.LastInputTime = Time::MonotonicMs();

    auto* current = state.GetCurrentMenu();
    if (current)
    {
        // Move cursor onto the first selectable row so disabled and Text entries are not
        // greeted as the initial selection.
        if (!current->Items.empty() && !IsCursorTarget(current->Items[0]))
            StepCursor(current->Items, state.SelectedIndex, +1);

        Log::Info("Menu opened for slot {} (title: {}, items: {})", slot, current->Title, current->Items.size());
    }
}

void MenuManager::CloseMenu(int slot)
{
    if (!IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return;

    state.MenuStack.pop();

    if (state.MenuStack.empty())
    {
        SetPlayerFrozen(slot, false);
        _messages.ClearCenterHtml(slot);
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
    if (!IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    SetPlayerFrozen(slot, false);
    state.Reset();
    _messages.ClearCenterHtml(slot);
}

void MenuManager::CloseAllWithReply(int slot, std::string_view key)
{
    // Translate before closing: the reply is addressed to a player whose menus are about to go.
    if (auto& reply = _policy.Reply; reply)
        reply(slot, _translations.Get(std::string(key), slot));

    CloseAllMenus(slot);
}

void MenuManager::BeginInput(int slot, std::string prompt, ChatInput::Callback callback)
{
    _chatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
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

    Pawn pawn = _entities.PawnOf(slot);
    if (!pawn)
        return;

    if (frozen)
        state.PrevMoveType = pawn.Move();

    pawn.SetMove(frozen ? MoveType::None : state.PrevMoveType);
    state.MovementFrozen = frozen;
}

void MenuManager::SetFreezePlayer(bool enabled)
{
    _freezePlayer = enabled;

    if (enabled)
        return;

    // Turning it off releases whoever the previous setting already froze; leaving them stuck
    // until they close a menu they may not know is open is not a defensible reading of "off".
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].MovementFrozen)
            SetPlayerFrozen(slot, false);
    }
}

bool MenuManager::HasActiveMenu(int slot) const
{
    if (!IsValidSlot(slot))
        return false;

    return _states[slot].HasMenu();
}

void MenuManager::OnGameFrame()
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
            currentOption->OnActivate(slot, *this);
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

    // While a capture is pending, render a prompt overlay instead of the item list.
    if (auto prompt = _chatInput.GetPrompt(slot))
    {
        _messages.SendCenterHtml(slot, RenderCaptureOverlay(menu->Title, *prompt));
        return;
    }

    bool isSubmenu = state.MenuStack.size() > 1;
    _messages.SendCenterHtml(slot, RenderMenuHtml(menu, slot, state.SelectedIndex, isSubmenu, _translations));
}

}  // namespace VoltMod
