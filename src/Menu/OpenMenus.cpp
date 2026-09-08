#include "Menu/OpenMenus.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <cstddef>
#include <string>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view kBreadcrumbSeparator = " › ";

void OpenMenus::BindReset(SlotEvents& slots)
{
    _states.BindReset(slots);
    _cursor.BindReset(slots);
    _pending.BindReset(slots);
}

Menu* OpenMenus::Current(int slot)
{
    return IsValidSlot(slot) ? _states[slot].GetCurrentMenu() : nullptr;
}

int OpenMenus::Depth(int slot) const
{
    return IsValidSlot(slot) ? static_cast<int>(_states[slot].MenuStack.size()) : 0;
}

bool OpenMenus::IsOpen(int slot) const
{
    return IsValidSlot(slot) && _states[slot].HasMenu();
}

bool OpenMenus::AnyOpen() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (_states[slot].HasMenu())
            return true;
    }
    return false;
}

bool OpenMenus::KeyboardEnabled(int slot) const
{
    return IsValidSlot(slot) && _states[slot].Keyboard;
}

std::string_view OpenMenus::Breadcrumb(int slot) const
{
    return IsValidSlot(slot) ? std::string_view(_states[slot].Breadcrumb) : std::string_view{};
}

void OpenMenus::Push(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot))
        return;

    _states[slot].MenuStack.push_back(std::move(menu));
    ResetCursor(slot);
}

bool OpenMenus::Pop(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    // Before the stack moves: a value stepped and left showing is applied, not dropped.
    _pending.Run(slot);

    auto& state = _states[slot];
    if (state.MenuStack.empty())
        return true;

    state.MenuStack.pop_back();
    if (state.MenuStack.empty())
    {
        state.Reset();
        _cursor.Select(slot, 0);
        return true;
    }

    ResetCursor(slot);
    return false;
}

void OpenMenus::Clear(int slot)
{
    if (!IsValidSlot(slot))
        return;

    _pending.Run(slot);
    _states[slot].Reset();
    _cursor.Select(slot, 0);
}

void OpenMenus::RunPending()
{
    _pending.RunAll();
}

void OpenMenus::ResetCursor(int slot)
{
    auto& state = _states[slot];
    state.LastInputTime = Time::MonotonicMs();
    state.Rows.clear();

    // Everything under the top menu, which is the path taken to reach what is on screen; the
    // current title is drawn on its own.
    state.Breadcrumb.clear();
    for (std::size_t i = 0; i + 1 < state.MenuStack.size(); ++i)
    {
        if (!state.Breadcrumb.empty())
            state.Breadcrumb += kBreadcrumbSeparator;
        state.Breadcrumb += state.MenuStack[i]->Title;
    }

    _cursor.Select(slot, state.GetCurrentMenu() ? MenuCursor::First(Rows(slot)) : 0);
}

MenuRow OpenMenus::Describe(int slot, int index)
{
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return MenuRow{.Enabled = false, .Selectable = false};

    // An item with no Describe is malformed; it draws as an inert line rather than a row the
    // cursor could land on.
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

CursorRows OpenMenus::Rows(int slot)
{
    auto* menu = Current(slot);
    if (!menu)
        return {};

    return {.Count = static_cast<int>(menu->Items.size()), .Landable = [menu, slot](int index) {
                return IsCursorTarget(menu->Items[static_cast<std::size_t>(index)], slot);
            }};
}

int OpenMenus::Selected(int slot) const
{
    return _cursor.Selected(slot);
}

void OpenMenus::Select(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // A press naming a row this menu does not have - a page that moved under a click, an id a
    // client made up - is dropped rather than parking the cursor past the end.
    auto* menu = Current(slot);
    if (index < 0 || !menu || index >= static_cast<int>(menu->Items.size()))
        return;

    // Leaving a stepped row applies what it was left showing. Landing back on the row that is
    // still waiting leaves it waiting, so W-then-S over one row is not an action.
    if (!_pending.IsPending(slot, index))
        _pending.Run(slot);

    _cursor.Select(slot, index);
}

void OpenMenus::SelectOnPage(int slot, int page, int rowsPerPage)
{
    auto* menu = Current(slot);
    if (!menu || menu->Items.empty() || rowsPerPage <= 0)
        return;

    Select(slot, MenuCursor::OnPage(Rows(slot), page, rowsPerPage));
}

void OpenMenus::Activate(int slot, int index)
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

    // Re-read: running a commit may have closed or replaced the menu.
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copied out of the vector, not referenced into it: a row that closes or reopens the menu
    // destroys the Menu, and with it the item whose handler is still running.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsCursorTarget(item, slot))
        item.Activate(slot, _session);
}

bool OpenMenus::Step(int slot, int index, int direction)
{
    auto* menu = Current(slot);
    if (!IsValidSlot(slot) || !menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copied for the same reason as in Activate, and the menu held so it cannot be freed and
    // another allocated at the same address while the step runs.
    const std::shared_ptr<Menu> held = _states[slot].MenuStack.back();
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    if (!item.Step(slot, direction))
        return false;

    // Holding the commit turns a burst of presses into one action; the row draws as pending until
    // it runs. Unless the step replaced the menu, in which case the row it belonged to is gone and
    // arming by index would aim at whatever now sits there.
    if (item.Commit && Current(slot) == held.get())
        _pending.Arm(slot, index, [commit = item.Commit, slot] { commit(slot); });

    return true;
}

}  // namespace VoltMod
