#pragma once

#include "Menu/MenuCursor.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuStack.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief A @ref MenuStack read with a cursor and movement keys.
 *
 * The half of a center-HTML session the surface owns: which row is selected, which buttons were
 * held last frame, and when the last press was acted on. Everything a Panorama surface would also
 * need lives in the stack underneath and is reached through it.
 */
class ActiveMenus
{
public:
    /** Referenced objects must outlive this instance. */
    ActiveMenus(MenuSession& session, Translations& translations, PendingCommit::Timer timer)
        : _stack(session, translations, std::move(timer))
    {}

    /** Clear a slot when it changes hands. */
    void BindReset(SlotEvents& slots);

    /** The renderer-agnostic half: stack, breadcrumb, Describe, Activate and Step. */
    [[nodiscard]] MenuStack& Stack() { return _stack; }

    [[nodiscard]] Menu* Current(int slot) { return _stack.Current(slot); }
    [[nodiscard]] int Depth(int slot) const { return _stack.Depth(slot); }
    [[nodiscard]] bool IsOpen(int slot) const { return _stack.IsOpen(slot); }
    [[nodiscard]] bool AnyOpen() const { return _stack.AnyOpen(); }
    [[nodiscard]] std::string_view Breadcrumb(int slot) const { return _stack.Breadcrumb(slot); }
    [[nodiscard]] MenuRow Describe(int slot, int index) { return _stack.Describe(slot, index); }
    void Activate(int slot, int index) { _stack.Activate(slot, index); }
    bool Step(int slot, int index, int direction) { return _stack.Step(slot, index, direction); }

    /** Push @p menu and put the cursor on its first selectable row. */
    void Push(int slot, std::shared_ptr<Menu> menu);

    /** @copydoc MenuStack::Pop. The cursor follows the menu that is showing again. */
    bool Pop(int slot);

    void Clear(int slot);

    /** Buttons held last frame, for edge detection. */
    [[nodiscard]] uint64_t& PrevButtons(int slot) { return _cursors[slot].PrevButtons; }

    /** Monotonic milliseconds of the last key this session acted on. */
    [[nodiscard]] int64_t& LastInputTime(int slot) { return _cursors[slot].LastInputTime; }

    /** Cursor input for the current menu. Valid for one move. */
    [[nodiscard]] CursorRows Rows(int slot);

    [[nodiscard]] int Selected(int slot) const { return IsValidSlot(slot) ? _cursors[slot].Selected : 0; }

    /** Put @p slot's cursor on row @p index, applying whatever the row it leaves was holding. An
     *  index the current menu does not have is dropped. */
    void Select(int slot, int index);

private:
    struct Cursor
    {
        int Selected = 0;
        uint64_t PrevButtons = 0;
        int64_t LastInputTime = 0;
    };

    /** Put the cursor back on the first selectable row of whatever is now on top. */
    void ResetCursor(int slot);

    MenuStack _stack;
    PerSlot<Cursor> _cursors;
};

}  // namespace VoltMod
