#include "Menu/MenuCursor.hpp"

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

static bool Landable(const CursorRows& rows, int index)
{
    if (index < 0 || index >= rows.Count)
        return false;

    return !rows.Landable || rows.Landable(index);
}

int MenuCursor::Step(const CursorRows& rows, int index, int step)
{
    if (rows.Count <= 0)
        return index;

    // Bound the search so an all-disabled menu cannot loop forever.
    int attempts = rows.Count;
    do
    {
        index = WrapIndex(index + step, rows.Count);
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

int MenuCursor::JumpPage(const CursorRows& rows, int index, int rowsPerPage, int delta)
{
    if (rows.Count <= 0 || rowsPerPage <= 0)
        return index;

    const int pages = PageCount(rows.Count, rowsPerPage);
    const int page = WrapIndex(index / rowsPerPage + delta, pages);
    const int start = page * rowsPerPage;
    const int end = std::min(rows.Count, start + rowsPerPage);

    // Preserve the page offset, then skip rows that cannot be selected.
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
