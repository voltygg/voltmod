#include "Menu/MenuCursor.hpp"
#include "Menu/MenuKeys.hpp"
#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <cstddef>
#include <string>

// The row half of the manager: what a driver asks about the menu it is drawing, and what a press
// does to it. The session itself - the stack, the drivers, the frame loop, the freeze - is in
// MenuManager.cpp.

namespace VoltMod
{

/** What a breadcrumb puts between two titles. */
static constexpr std::string_view kBreadcrumbSeparator = " › ";

Menu* MenuManager::Current(int slot)
{
    return IsValidSlot(slot) ? _states[slot].GetCurrentMenu() : nullptr;
}

int MenuManager::Depth(int slot) const
{
    return IsValidSlot(slot) ? static_cast<int>(_states[slot].MenuStack.size()) : 0;
}

bool MenuManager::KeyboardEnabled(int slot) const
{
    return IsValidSlot(slot) && _states[slot].Keyboard;
}

bool MenuManager::HandleKeys(int slot, MenuDriver& driver)
{
    return _keys->Handle(slot, driver);
}

std::string_view MenuManager::Breadcrumb(int slot) const
{
    return IsValidSlot(slot) ? std::string_view(_states[slot].Breadcrumb) : std::string_view{};
}

int MenuManager::Selected(int slot) const
{
    return _cursor->Selected(slot);
}

void MenuManager::Select(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // Leaving a stepped row applies what it was left showing. Landing back on the row that is
    // still waiting leaves it waiting, so W-then-S over one row is not an action.
    if (!_pending->IsPending(slot, index))
        _pending->Run(slot);

    _cursor->Select(slot, index);
}

void MenuManager::SelectOnPage(int slot, int page, int rowsPerPage)
{
    auto* menu = Current(slot);
    if (!menu || menu->Items.empty() || rowsPerPage <= 0)
        return;

    Select(slot, MenuCursor::OnPage(CursorRowsOf(menu->Items, slot), page, rowsPerPage));
}

void MenuManager::ResetCursor(int slot)
{
    if (!IsValidSlot(slot))
        return;

    auto& state = _states[slot];
    state.LastInputTime = Time::MonotonicMs();
    state.Rows.clear();

    // Everything under the top menu, which is the path taken to reach what is on screen; the
    // current title is drawn on its own and would only repeat itself here.
    state.Breadcrumb.clear();
    for (std::size_t i = 0; i + 1 < state.MenuStack.size(); ++i)
    {
        if (!state.Breadcrumb.empty())
            state.Breadcrumb += kBreadcrumbSeparator;
        state.Breadcrumb += state.MenuStack[i]->Title;
    }

    auto* menu = state.GetCurrentMenu();
    _cursor->Select(slot, menu ? MenuCursor::First(CursorRowsOf(menu->Items, slot)) : 0);
}

MenuRow MenuManager::Describe(int slot, int index)
{
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return MenuRow{.Enabled = false, .Selectable = false};

    // An item with no Describe is malformed; it draws as an inert line rather than a row the
    // cursor could land on.
    const MenuItem& item = menu->Items[static_cast<std::size_t>(index)];
    MenuRow row = item.Describe ? item.Describe(slot) : MenuRow{.Enabled = false, .Selectable = false};

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
    row.Pending = _pending->IsPending(slot, index);
    return row;
}

void MenuManager::Activate(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // A row whose activation *is* its commit - a ChoiceRow's E - would apply the value twice if
    // the held one ran as well, so pressing the pending row cancels the wait and lets the
    // activation apply it. Any other row runs what is held first, then does its own thing.
    if (_pending->IsPending(slot, index))
        _pending->Cancel(slot);
    else
        _pending->Run(slot);

    // Re-read: running a commit may have closed or replaced the menu.
    auto* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copies out of the vector, not references into it: a row that closes or reopens the menu
    // destroys the Menu, and with it the item whose handler is still running.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsCursorTarget(item, slot))
        item.Activate(slot, *this);
}

bool MenuManager::Step(int slot, int index, int direction)
{
    auto* menu = Current(slot);
    if (!IsValidSlot(slot) || !menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copied for the same reason as in Activate: a step that persists may rebuild the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    if (!item.Step(slot, direction))
        return false;

    // The row now shows a value nothing has applied. Holding the commit is what turns a burst of
    // presses into one action; the row draws as pending until it runs.
    if (item.Commit)
        _pending->Arm(slot, index, [commit = item.Commit, slot] { commit(slot); });

    return true;
}

}  // namespace VoltMod
