#include "Menu/ActiveMenus.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Time.hpp>
#include <cstddef>
#include <utility>

namespace VoltMod
{

void ActiveMenus::BindReset(SlotEvents& slots)
{
    _stack.BindReset(slots);
    _cursors.BindReset(slots);
}

void ActiveMenus::Push(int slot, std::shared_ptr<Menu> menu)
{
    _stack.Push(slot, std::move(menu));
    ResetCursor(slot);
}

bool ActiveMenus::Pop(int slot)
{
    if (_stack.Pop(slot))
    {
        if (IsValidSlot(slot))
            _cursors[slot] = {};
        return true;
    }

    ResetCursor(slot);
    return false;
}

void ActiveMenus::Clear(int slot)
{
    _stack.Clear(slot);
    if (IsValidSlot(slot))
        _cursors[slot] = {};
}

void ActiveMenus::ResetCursor(int slot)
{
    if (!IsValidSlot(slot))
        return;

    Cursor& cursor = _cursors[slot];
    cursor.LastInputTime = Time::MonotonicMs();
    cursor.Selected = _stack.Current(slot) ? MenuCursor::First(Rows(slot)) : 0;
}

CursorRows ActiveMenus::Rows(int slot)
{
    Menu* menu = _stack.Current(slot);
    if (!menu)
        return {};

    return {.Count = static_cast<int>(menu->Items.size()), .Landable = [menu, slot](int index) {
                return IsRowActionable(menu->Items[static_cast<std::size_t>(index)], slot);
            }};
}

void ActiveMenus::Select(int slot, int index)
{
    if (!IsValidSlot(slot))
        return;

    // Ignore stale or client-forged row indexes.
    Menu* menu = _stack.Current(slot);
    if (index < 0 || !menu || index >= static_cast<int>(menu->Items.size()))
        return;

    // Leaving a stepped row applies its pending value. Returning to it leaves the value pending.
    if (!_stack.IsPending(slot, index))
        _stack.RunPending(slot);

    _cursors[slot].Selected = index;
}

}  // namespace VoltMod
