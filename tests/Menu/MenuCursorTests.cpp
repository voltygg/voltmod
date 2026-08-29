#include "Menu/MenuCursor.hpp"

#include <doctest/doctest.h>
#include <set>

using VoltMod::CursorRows;
using VoltMod::MenuCursor;

static CursorRows Rows(int count, std::set<int> landable)
{
    return {.Count = count,
            .Landable = [landable = std::move(landable)](int index) { return landable.contains(index); }};
}

static CursorRows AllRows(int count)
{
    return {.Count = count};
}

TEST_CASE("MenuCursor: Selected starts at the first row and Select moves it")
{
    MenuCursor cursor;

    CHECK(cursor.Selected(0) == 0);

    cursor.Select(0, 3);
    CHECK(cursor.Selected(0) == 3);
    CHECK(cursor.Selected(1) == 0);

    // A slot no player can occupy answers rather than indexing out of range.
    CHECK(cursor.Selected(-1) == 0);
    cursor.Select(-1, 3);
    CHECK(cursor.Selected(-1) == 0);
}

TEST_CASE("MenuCursor: Step wraps in both directions")
{
    const CursorRows rows = AllRows(3);

    CHECK(MenuCursor::Step(rows, 2, +1) == 0);
    CHECK(MenuCursor::Step(rows, 0, -1) == 2);
    CHECK(MenuCursor::Step(rows, 0, +1) == 1);
}

TEST_CASE("MenuCursor: Step skips rows the cursor may not land on")
{
    const CursorRows rows = Rows(5, {0, 3});

    CHECK(MenuCursor::Step(rows, 0, +1) == 3);
    CHECK(MenuCursor::Step(rows, 3, +1) == 0);
    CHECK(MenuCursor::Step(rows, 0, -1) == 3);
}

TEST_CASE("MenuCursor: an all-disabled menu leaves the index where it was")
{
    const CursorRows rows = Rows(4, {});

    CHECK(MenuCursor::Step(rows, 2, +1) == 2);
    CHECK(MenuCursor::Step(rows, 2, -1) == 2);
    CHECK(MenuCursor::First(rows) == 0);
    CHECK(MenuCursor::JumpPage(rows, 1, 2, +1) == 3);
}

TEST_CASE("MenuCursor: an empty menu leaves every move where it started")
{
    const CursorRows rows = AllRows(0);

    CHECK(MenuCursor::Step(rows, 0, +1) == 0);
    CHECK(MenuCursor::First(rows) == 0);
    CHECK(MenuCursor::OnPage(rows, 1, 4) == 0);
    CHECK(MenuCursor::JumpPage(rows, 0, 4, +1) == 0);
}

TEST_CASE("MenuCursor: First lands on the first row the cursor may land on")
{
    CHECK(MenuCursor::First(AllRows(3)) == 0);
    CHECK(MenuCursor::First(Rows(4, {2, 3})) == 2);
}

TEST_CASE("MenuCursor: OnPage lands on the first target of the page")
{
    const CursorRows rows = Rows(10, {0, 5, 6});

    CHECK(MenuCursor::OnPage(rows, 0, 5) == 0);
    CHECK(MenuCursor::OnPage(rows, 1, 5) == 5);

    // A page whose rows are all disabled still puts the cursor inside it, not on the last page's
    // selection.
    CHECK(MenuCursor::OnPage(Rows(10, {0}), 1, 5) == 5);

    // Past the last page clamps into range rather than selecting a row that is not there.
    CHECK(MenuCursor::OnPage(rows, 9, 5) == 9);
}

TEST_CASE("MenuCursor: JumpPage keeps the offset within the page")
{
    const CursorRows rows = AllRows(12);

    CHECK(MenuCursor::JumpPage(rows, 1, 5, +1) == 6);
    CHECK(MenuCursor::JumpPage(rows, 6, 5, -1) == 1);

    // Wraps: the third page holds two rows, so an offset of 1 is the last of them.
    CHECK(MenuCursor::JumpPage(rows, 6, 5, +1) == 11);
    CHECK(MenuCursor::JumpPage(rows, 1, 5, -1) == 11);
}

TEST_CASE("MenuCursor: JumpPage skips forward inside the page it lands on")
{
    const CursorRows rows = Rows(10, {0, 7});

    // Offset 2 of the second page is disabled, so the cursor walks on to the next row that is not.
    CHECK(MenuCursor::JumpPage(rows, 2, 5, +1) == 7);
    // Nothing after the offset takes it, so the search wraps within the page back to its top.
    CHECK(MenuCursor::JumpPage(Rows(10, {0, 5}), 3, 5, +1) == 5);
}
