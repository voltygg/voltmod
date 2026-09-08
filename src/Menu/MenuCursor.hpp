#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <functional>

namespace VoltMod
{

/** Rows available to cursor navigation. */
struct CursorRows
{
    /** Number of rows. */
    int Count = 0;

    /** True when the cursor may land on @p index. Unset means every row takes it. */
    std::function<bool(int index)> Landable;
};

/** True when the cursor may land on @p item. */
[[nodiscard]] bool IsCursorTarget(const MenuItem& item, int slot);

namespace MenuCursor
{
/** @p index moved by @p step, wrapping over @p rows and skipping what it may not land on. */
[[nodiscard]] int Step(const CursorRows& rows, int index, int step);

/** First selectable row. */
[[nodiscard]] int First(const CursorRows& rows);

/** First selectable row on @p page, or the page's first row. */
[[nodiscard]] int OnPage(const CursorRows& rows, int page, int rowsPerPage);

/** Moves @p index by @p delta pages while preserving its page offset. */
[[nodiscard]] int JumpPage(const CursorRows& rows, int index, int rowsPerPage, int delta);
}  // namespace MenuCursor

}  // namespace VoltMod
