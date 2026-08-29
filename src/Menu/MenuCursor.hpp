#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <vector>

namespace VoltMod
{

/**
 * @brief The rows a cursor moves over: how many there are, and which of them it may land on.
 *
 * A row is an index rather than a @ref MenuItem, so the arithmetic does not have to know what a
 * menu is - which is what lets the rules be driven from a test table.
 */
struct CursorRows
{
    /** How many rows the menu has. Zero leaves every move where it started. */
    int Count = 0;

    /** True when the cursor may land on @p index. Unset means every row takes it. */
    std::function<bool(int index)> Landable;
};

/** True when the cursor may land on @p item as it describes itself to @p slot. A row with no
 *  @ref MenuItem::Describe is malformed and refuses it, as a disabled or Text row does. */
[[nodiscard]] bool IsCursorTarget(const MenuItem& item, int slot);

/** @p items as rows a cursor moves over, asking each one how it describes itself to @p slot.
 *  @p items must outlive the returned value, which one call's worth of moves gives. */
[[nodiscard]] CursorRows CursorRowsOf(const std::vector<MenuItem>& items, int slot);

/**
 * @brief Where each player's cursor is, and where a step, a page turn or a new menu puts it.
 *
 * SDK-free: the moves are pure functions of a @ref CursorRows, and the only state is one index
 * per slot, dropped when the slot changes hands. @ref MenuManager owns one and is the only
 * writer - both drivers and the shared keys go through it, so switching drivers mid-session
 * cannot leave two cursors disagreeing.
 *
 * Every move skips rows the cursor may not land on. A menu with no landable row at all leaves
 * the index where it was rather than parking on a row that refuses input.
 */
class MenuCursor
{
public:
    /** Send a slot's cursor back to the first row when a player joins or leaves it. */
    void BindReset(SlotEvents& slots) { _selected.BindReset(slots); }

    /** Where @p slot's cursor is; 0 for a slot that cannot hold one. */
    [[nodiscard]] int Selected(int slot) const;

    /** Put @p slot's cursor on @p index. */
    void Select(int slot, int index);

    /** @p index moved by @p step, wrapping over @p rows and skipping what it may not land on. */
    [[nodiscard]] static int Step(const CursorRows& rows, int index, int step);

    /** The first row the cursor may land on, so a disabled or Text row is never where a freshly
     *  opened menu starts. */
    [[nodiscard]] static int First(const CursorRows& rows);

    /** The first landable row of @p page, or the top of that page when it holds none - for a
     *  driver whose page turned without the cursor. */
    [[nodiscard]] static int OnPage(const CursorRows& rows, int page, int rowsPerPage);

    /** @p index moved @p delta pages, keeping its offset within the page and then skipping
     *  forward over rows it may not land on inside the new one. */
    [[nodiscard]] static int JumpPage(const CursorRows& rows, int index, int rowsPerPage, int delta);

private:
    /** True when @p index is a row of @p rows and @ref CursorRows::Landable takes it. */
    static bool Landable(const CursorRows& rows, int index);

    PerSlot<int> _selected;
};

}  // namespace VoltMod
