#include "Menu/ActiveMenus.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <cstddef>
#include <string>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view kBreadcrumbSeparator = " › ";

void ActiveMenus::BindReset(SlotEvents& slots)
{
    _states.BindReset(slots);
    _pending.BindReset(slots);
}

bool ActiveMenus::AnyOpen() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            return true;
    }
    return false;
}

void ActiveMenus::Push(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot))
        return;

    _states[slot].MenuStack.push_back(std::move(menu));
    ResetCursor(slot);
}

bool ActiveMenus::Pop(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    // Apply a stepped value before changing the stack.
    _pending.Run(slot);

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return true;

    state.MenuStack.pop_back();
    if (state.MenuStack.empty())
    {
        state.Reset();
        return true;
    }

    ResetCursor(slot);
    return false;
}

void ActiveMenus::Clear(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _pending.Run(slot);
    _states[slot].Reset();
}

void ActiveMenus::ResetCursor(int slot)
{
    auto& state = _states[slot];
    state.LastInputTime = Time::MonotonicMs();
    state.Rows.clear();

    // Build the path leading to the current menu.
    state.Breadcrumb.clear();
    for (std::size_t i = 0; i + 1 < state.MenuStack.size(); ++i)
    {
        if (!state.Breadcrumb.empty())
            state.Breadcrumb += kBreadcrumbSeparator;
        state.Breadcrumb += state.MenuStack[i]->Title;
    }

    state.Selected = state.GetCurrentMenu() ? MenuCursor::First(Rows(slot)) : 0;
}

MenuRow ActiveMenus::Describe(int slot, int index)
{
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return MenuRow{.Enabled = false, .Selectable = false};

    // A malformed item is shown as an inert line.
    const MenuItem& item = menu->Items[static_cast<std::size_t>(index)];
    MenuRow row = item.Describe ? item.Describe(slot) : MenuRow{.Enabled = false, .Selectable = false};

    // A toggle reports its state, not its words, so every switch reads the same and localizes here.
    if (row.Kind == MenuRowKind::Toggle && row.Value.empty() && row.State.has_value())
    {
        row.Value =
            *row.State ? _translations.GetOr("menu.on", slot, "ON") : _translations.GetOr("menu.off", slot, "OFF");
    }

    auto& state = _states[slot];
    if (state.Rows.size() != menu->Items.size())
        state.Rows.assign(menu->Items.size(), MenuRowMemory{});

    const int64_t now = Time::MonotonicMs();
    MenuRowMemory& memory = state.Rows[static_cast<std::size_t>(index)];
    if (memory.Value != row.Value)
    {
        // Arriving on screen is not a change: only a value that moves under a row already drawn
        // is worth flashing.
        if (memory.Drawn)
            memory.ChangedAt = now;
        memory.Value = row.Value;
        memory.Drawn = true;
    }

    row.Changed = memory.ChangedAt != 0 && now - memory.ChangedAt < ChangedMs;
    row.Pending = _pending.IsPending(slot, index);
    return row;
}

CursorRows ActiveMenus::Rows(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return {};

    return {.Count = static_cast<int>(menu->Items.size()), .Landable = [menu, slot](int index) {
                return IsCursorTarget(menu->Items[static_cast<std::size_t>(index)], slot);
            }};
}

void ActiveMenus::Select(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // Ignore stale or client-forged row indexes.
    auto* menu = Current(slot);
    if (index < 0 || !menu || index >= static_cast<int>(menu->Items.size()))
        return;

    // Leaving a stepped row applies its pending value. Returning to it leaves the value pending.
    if (!_pending.IsPending(slot, index))
        _pending.Run(slot);

    _states[slot].Selected = index;
}

void ActiveMenus::SelectOnPage(int slot, int page, int rowsPerPage)
{
    auto* menu = Current(slot);
    if (!menu || menu->Items.empty() || rowsPerPage <= 0)
        return;

    Select(slot, MenuCursor::OnPage(Rows(slot), page, rowsPerPage));
}

void ActiveMenus::Activate(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // A row whose activation *is* its commit - a ChoiceRow's E - would apply the value twice if
    // the held one ran as well, so pressing the pending row cancels the wait and lets the
    // activation apply it. Any other row runs what is held first.
    if (_pending.IsPending(slot, index))
        _pending.Cancel(slot);
    else
        _pending.Run(slot);

    // The callback may close or replace the menu, so read the session again.
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copy the item because its handler may close or reopen the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsCursorTarget(item, slot))
        item.Activate(slot, _session);
}

bool ActiveMenus::Step(int slot, int index, int direction)
{
    auto* menu = Current(slot);
    if (!IsValidSlot(slot) || !menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copy the item and keep the menu alive while its step handler runs.
    const std::shared_ptr<Menu> held = _states[slot].MenuStack.back();
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    if (!item.Step(slot, direction))
        return false;

    // Coalesce a burst of presses. Do not re-arm by index if the step replaced the menu.
    if (item.Commit && Current(slot) == held.get())
        _pending.Arm(slot, index, [commit = item.Commit, slot] { commit(slot); });

    return true;
}

}  // namespace VoltMod
