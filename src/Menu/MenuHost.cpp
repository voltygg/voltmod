#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/MenuHost.hpp>
#include <memory>
#include <utility>

namespace VoltMod
{

MenuHost::MenuHost(SlotEvents& slots, EntitySystem& entities, ChatInput& chatInput, Translations& translations,
                   Policy& policy)
    : _entities(entities), _chatInput(chatInput), _translations(translations), _policy(policy)
{
    _states.BindReset(slots);
}

bool MenuHost::IsCursorTarget(const MenuItem& item, int slot)
{
    if (!item.Describe)
        return false;
    const MenuRow row = item.Describe(slot);
    return row.Enabled && row.Selectable;
}

void MenuHost::StepCursor(int slot, const std::vector<MenuItem>& items, int& idx, int step)
{
    int n = static_cast<int>(items.size());
    if (n == 0)
        return;

    int attempts = n;
    do
    {
        idx = ((idx + step) % n + n) % n;
    }
    while (!IsCursorTarget(items[idx], slot) && --attempts > 0);
}

void MenuHost::OpenMenu(int slot, std::shared_ptr<Menu> menu, MenuSessionOptions options)
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
    state.Page = 0;
    state.LastInputTime = Time::MonotonicMs();

    if (auto* current = state.GetCurrentMenu())
    {
        SelectFirst(slot);
        Log::Info("Menu opened for slot {} (title: {}, items: {})", slot, current->Title, current->Items.size());
    }
}

void MenuHost::CloseMenu(int slot)
{
    if (!IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return;

    state.MenuStack.pop();
    Log::Info("Menu closed for slot {} ({} left on the stack)", slot, state.MenuStack.size());

    if (state.MenuStack.empty())
    {
        SetPlayerFrozen(slot, false);
        Dismiss(slot);
        state.Reset();
        return;
    }

    state.SelectedIndex = 0;
    state.Page = 0;
    SelectFirst(slot);
}

void MenuHost::CloseAllMenus(int slot)
{
    if (!IsValidSlot(slot))
        return;

    SetPlayerFrozen(slot, false);
    _states[slot].Reset();
    Log::Info("All menus closed for slot {}", slot);
    Dismiss(slot);
}

void MenuHost::CloseAllWithReply(int slot, std::string_view key)
{
    // Translate before closing: the reply is addressed to a player whose menus are about to go.
    if (auto& reply = _policy.Reply; reply)
        reply(slot, _translations.Get(std::string(key), slot));

    CloseAllMenus(slot);
}

void MenuHost::BeginInput(int slot, std::string prompt, ChatInput::Callback callback)
{
    _chatInput.BeginCapture(slot, std::move(prompt), std::move(callback));
}

bool MenuHost::HasActiveMenu(int slot) const
{
    return IsValidSlot(slot) && _states[slot].HasMenu();
}

void MenuHost::SetFreezePlayer(bool enabled)
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

void MenuHost::Activate(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    auto* menu = _states[slot].GetCurrentMenu();
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copies out of the vector, not references into it: a row that closes or reopens the menu
    // destroys the Menu, and with it the item whose handler is still running.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsCursorTarget(item, slot))
        item.Activate(slot, *this);
}

bool MenuHost::Step(int slot, int index, int direction)
{
    if (!IsValidSlot(slot))
        return false;

    auto* menu = _states[slot].GetCurrentMenu();
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copied for the same reason as in Activate: a step that persists may rebuild the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    return item.Step(slot, direction);
}

void MenuHost::SetPlayerFrozen(int slot, bool frozen)
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

void MenuHost::SelectFirst(int slot)
{
    auto& state = _states[slot];
    auto* menu = state.GetCurrentMenu();
    if (menu && !menu->Items.empty() && !IsCursorTarget(menu->Items[0], slot))
        StepCursor(slot, menu->Items, state.SelectedIndex, +1);
}

}  // namespace VoltMod
