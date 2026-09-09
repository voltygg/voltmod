#pragma once

#include "Menu/MenuCursor.hpp"
#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace VoltMod
{

/** Per-player menu stacks, cursors, and row actions. */
class ActiveMenus
{
public:
    /** Referenced objects must outlive this instance. */
    ActiveMenus(MenuSession& session, Translations& translations, PendingCommit::Timer timer)
        : _session(session), _translations(translations), _pending(std::move(timer))
    {}

    /** Clear a slot when it changes hands. */
    void BindReset(SlotEvents& slots);

    [[nodiscard]] PlayerMenuState& State(int slot) { return _states[slot]; }

    /** Top menu, or null when none is open. */
    [[nodiscard]] Menu* Current(int slot) { return IsValidSlot(slot) ? _states[slot].GetCurrentMenu() : nullptr; }

    [[nodiscard]] int Depth(int slot) const
    {
        return IsValidSlot(slot) ? static_cast<int>(_states[slot].MenuStack.size()) : 0;
    }
    [[nodiscard]] bool IsOpen(int slot) const { return IsValidSlot(slot) && _states[slot].HasMenu(); }
    [[nodiscard]] bool AnyOpen() const;

    /** Parent menu titles joined as a breadcrumb. Valid until the stack changes. */
    [[nodiscard]] std::string_view Breadcrumb(int slot) const
    {
        return IsValidSlot(slot) ? std::string_view(_states[slot].Breadcrumb) : std::string_view{};
    }

    void Push(int slot, std::shared_ptr<Menu> menu);

    /** Pop the top menu, applying whatever a stepped row was left showing. True when the stack is
     *  now empty, which is the caller's cue to unfreeze and dismiss. */
    bool Pop(int slot);

    /** Clear the whole stack, applying whatever a stepped row was left showing. */
    void Clear(int slot);

    /** Row @p index as it describes itself, with @ref MenuRow::Pending and @ref MenuRow::Changed
     *  filled in and a Toggle's on/off word spelled. An index with no row behind it describes as
     *  an inert, unselectable line. */
    [[nodiscard]] MenuRow Describe(int slot, int index);

    /** Cursor input for the current menu. Valid for one move. */
    [[nodiscard]] CursorRows Rows(int slot);

    /** Run row @p index, as if it had been selected and confirmed. Ignores rows that are disabled,
     *  unselectable, or out of range.
     *
     *  Runs a commit held for another row first, and cancels one held for *this* row: a row whose
     *  activation is its own commit would otherwise apply the value twice. */
    void Activate(int slot, int index);

    /** Nudge row @p index's value by @p direction (-1 or +1). True when the row consumed it, which
     *  is what tells a keyboard driver to page instead. A row with a @ref MenuItem::Commit is
     *  stepped, not applied: the commit is held so a burst of presses runs one action. */
    bool Step(int slot, int index, int direction);

    [[nodiscard]] int Selected(int slot) const { return IsValidSlot(slot) ? _states[slot].Selected : 0; }

    /** Put @p slot's cursor on row @p index, applying whatever the row it leaves was holding. An
     *  index the current menu does not have is dropped. */
    void Select(int slot, int index);

private:
    void ResetCursor(int slot);

    static constexpr int64_t ChangedMs = 150;

    MenuSession& _session;
    Translations& _translations;

    PerSlot<PlayerMenuState> _states;
    PendingCommit _pending;
};

}  // namespace VoltMod
