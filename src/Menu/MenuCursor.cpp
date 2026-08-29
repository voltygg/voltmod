#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/Slot.hpp>
#include <algorithm>
#include <cstddef>

namespace VoltMod
{

bool IsCursorTarget(const MenuItem& item, int slot)
{
    if (!item.Describe)
        return false;

    const MenuRow row = item.Describe(slot);
    return row.Enabled && row.Selectable;
}

CursorRows CursorRowsOf(const std::vector<MenuItem>& items, int slot)
{
    return {.Count = static_cast<int>(items.size()), .Landable = [&items, slot](int index) {
                return IsCursorTarget(items[static_cast<std::size_t>(index)], slot);
            }};
}

int MenuCursor::Selected(int slot) const
{
    return IsValidSlot(slot) ? _selected[slot] : 0;
}

void MenuCursor::Select(int slot, int index)
{
    if (IsValidSlot(slot))
        _selected[slot] = index;
}

bool MenuCursor::Landable(const CursorRows& rows, int index)
{
    if (index < 0 || index >= rows.Count)
        return false;

    return !rows.Landable || rows.Landable(index);
}

int MenuCursor::Step(const CursorRows& rows, int index, int step)
{
    if (rows.Count <= 0)
        return index;

    // One attempt per row: a menu whose rows are all disabled walks the whole way round and comes
    // back to where it started rather than looping forever.
    int attempts = rows.Count;
    do
    {
        index = ((index + step) % rows.Count + rows.Count) % rows.Count;
    }
    while (!Landable(rows, index) && --attempts > 0);

    return index;
}

int MenuCursor::First(const CursorRows& rows)
{
    if (rows.Count <= 0 || Landable(rows, 0))
        return 0;

    return Step(rows, 0, +1);
}

int MenuCursor::OnPage(const CursorRows& rows, int page, int rowsPerPage)
{
    if (rows.Count <= 0 || rowsPerPage <= 0)
        return 0;

    const int start = std::clamp(page * rowsPerPage, 0, rows.Count - 1);
    const int end = std::min(rows.Count, start + rowsPerPage);

    int index = start;
    while (index < end && !Landable(rows, index))
        ++index;

    return index < end ? index : start;
}

int MenuCursor::JumpPage(const CursorRows& rows, int index, int rowsPerPage, int delta)
{
    if (rows.Count <= 0 || rowsPerPage <= 0)
        return index;

    const int pages = PageCount(rows.Count, rowsPerPage);
    const int page = ((index / rowsPerPage + delta) % pages + pages) % pages;
    const int start = page * rowsPerPage;
    const int end = std::min(rows.Count, start + rowsPerPage);

    // The offset within the page is preserved, then the cursor skips forward over rows it may
    // not land on within the new page.
    int landed = std::min(start + index % rowsPerPage, end - 1);
    for (int attempts = end - start; attempts > 0; --attempts)
    {
        if (Landable(rows, landed))
            break;
        landed = (landed + 1 < end) ? landed + 1 : start;
    }

    return landed;
}

}  // namespace VoltMod
