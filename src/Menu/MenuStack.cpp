#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Menu/MenuStack.hpp>
#include <cstddef>
#include <utility>

namespace VoltMod
{

static constexpr std::string_view kBreadcrumbSeparator = " › ";

MenuStack::MenuStack(MenuSurface& surface, Translations& translations, PendingCommit::Timer timer)
    : _surface(surface), _translations(translations), _pending(std::move(timer))
{}

void MenuStack::BindReset(SlotEvents& slots)
{
    _states.BindReset(slots);
    _pending.BindReset(slots);
}

Menu* MenuStack::Current(int slot)
{
    if (!IsValidSlot(slot) || _states[slot].Menus.empty())
        return nullptr;
    return _states[slot].Menus.back().get();
}

Menu* MenuStack::Root(int slot)
{
    if (!IsValidSlot(slot) || _states[slot].Menus.empty())
        return nullptr;
    return _states[slot].Menus.front().get();
}

int MenuStack::Depth(int slot) const
{
    return IsValidSlot(slot) ? static_cast<int>(_states[slot].Menus.size()) : 0;
}

bool MenuStack::IsOpen(int slot) const
{
    return Depth(slot) > 0;
}

bool MenuStack::AnyOpen() const
{
    for (int slot = 0; slot < MaxPlayers; ++slot)
    {
        if (!_states[slot].Menus.empty())
            return true;
    }
    return false;
}

std::string_view MenuStack::Breadcrumb(int slot) const
{
    return IsValidSlot(slot) ? std::string_view(_states[slot].Breadcrumb) : std::string_view{};
}

void MenuStack::Push(int slot, std::shared_ptr<Menu> menu)
{
    if (!IsValidSlot(slot) || !menu)
        return;

    _states[slot].Menus.push_back(std::move(menu));
    Rebuild(slot);
}

bool MenuStack::Pop(int slot)
{
    if (!IsValidSlot(slot))
        return false;

    // Apply a stepped value before changing the stack.
    RunPending(slot);

    State& state = _states[slot];
    if (state.Menus.empty())
        return true;

    state.Menus.pop_back();
    if (state.Menus.empty())
    {
        state = {};
        return true;
    }

    Rebuild(slot);
    return false;
}

void MenuStack::Clear(int slot)
{
    if (!IsValidSlot(slot))
        return;

    RunPending(slot);
    _states[slot] = {};
}

void MenuStack::Rewind(int slot)
{
    if (!IsValidSlot(slot) || _states[slot].Menus.empty())
        return;

    RunPending(slot);
    _states[slot].Menus.resize(1);
    Rebuild(slot);
}

void MenuStack::Rebuild(int slot)
{
    State& state = _states[slot];
    state.Rows.clear();

    // The path leading to the current menu: every title below the top one.
    state.Breadcrumb.clear();
    for (std::size_t i = 0; i + 1 < state.Menus.size(); ++i)
    {
        if (!state.Breadcrumb.empty())
            state.Breadcrumb += kBreadcrumbSeparator;
        state.Breadcrumb += state.Menus[i]->Title;
    }
}

MenuRow MenuStack::Describe(int slot, int index)
{
    Menu* menu = Current(slot);
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

    State& state = _states[slot];
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

void MenuStack::Activate(int slot, int index)
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

    // The callback may close or replace the menu, so read the stack again.
    Menu* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return;

    // Copy the item because its handler may close or reopen the menu.
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (item.Activate && IsRowActionable(item, slot))
        item.Activate(slot, _surface);
}

bool MenuStack::Step(int slot, int index, int direction)
{
    Menu* menu = Current(slot);
    if (!menu || index < 0 || index >= static_cast<int>(menu->Items.size()))
        return false;

    // Copy the item and keep the menu alive while its step handler runs.
    const std::shared_ptr<Menu> held = _states[slot].Menus.back();
    const MenuItem item = menu->Items[static_cast<std::size_t>(index)];
    if (!item.Step || !item.Describe || !item.Describe(slot).Enabled)
        return false;
    if (!item.Step(slot, direction))
        return false;

    // Coalesce a burst of presses. Do not re-arm by index if the step replaced the menu.
    if (item.Commit && Current(slot) == held.get())
        _pending.Arm(slot, index, [this, commit = item.Commit, slot] {
            commit(slot);
            Committed.Raise(slot);
        });

    return true;
}

void MenuStack::RunPending(int slot)
{
    _pending.Run(slot);
}

bool MenuStack::IsPending(int slot, int index) const
{
    return _pending.IsPending(slot, index);
}

}  // namespace VoltMod
