#pragma once

#include "Menu/MenuCursor.hpp"
#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/PerSlot.hpp>
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

/**
 * @brief What each player has open, where their cursor sits, and what a press does to a row.
 *
 * Both drivers and the shared key handler hold one of these directly, so they cannot end up with
 * separate cursors. @ref MenuManager owns it and keeps the engine half: the freeze, the frame
 * subscription, the driver choice, the chat prompt. Nothing here touches the SDK.
 */
class OpenMenus
{
public:
    /** @p session is handed to a row's @ref MenuItem::Activate and @p translations spells the
     *  words the framework supplies for a row; both must outlive this. @p timer runs the step
     *  debounce. */
    OpenMenus(MenuSession& session, Translations& translations, PendingCommit::Timer timer)
        : _session(session), _translations(translations), _pending(std::move(timer))
    {}

    /** Clear a slot's stack, cursor and pending commit when it changes hands. */
    void BindReset(SlotEvents& slots);

    /** @p slot's session: the stack, the freeze bookkeeping and the keys read for it. */
    [[nodiscard]] PlayerMenuState& State(int slot) { return _states[slot]; }

    /** Top of @p slot's stack, or null when nothing is open. */
    [[nodiscard]] Menu* Current(int slot);

    [[nodiscard]] int Depth(int slot) const;
    [[nodiscard]] bool IsOpen(int slot) const;
    [[nodiscard]] bool AnyOpen() const;

    /** Whether keys drive @p slot's session (@ref MenuOptions::Keyboard). */
    [[nodiscard]] bool KeyboardEnabled(int slot) const;

    /** The titles under the current menu, joined; empty at the root. Valid until the stack moves. */
    [[nodiscard]] std::string_view Breadcrumb(int slot) const;

    void Push(int slot, std::shared_ptr<Menu> menu);

    /** Pop the top menu, applying whatever a stepped row was left showing. True when the stack is
     *  now empty, which is the caller's cue to unfreeze and dismiss. */
    bool Pop(int slot);

    /** Clear the whole stack, applying whatever a stepped row was left showing. */
    void Clear(int slot);

    /** Apply every player's pending commit, for a driver swap that ends every session at once. */
    void RunPending();

    /** Row @p index as it describes itself, with @ref MenuRow::Pending and @ref MenuRow::Changed
     *  filled in and a Toggle's on/off word spelled. An index with no row behind it describes as
     *  an inert, unselectable line. */
    [[nodiscard]] MenuRow Describe(int slot, int index);

    /** The rows @p slot's cursor may move over. Valid for one move. */
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

    [[nodiscard]] int Selected(int slot) const;

    /** Put @p slot's cursor on row @p index, applying whatever the row it leaves was holding. An
     *  index the current menu does not have is dropped. */
    void Select(int slot, int index);

    /** Put the cursor on the first row it may land on within @p page, for a driver whose page
     *  turned without it. */
    void SelectOnPage(int slot, int page, int rowsPerPage);

private:
    /** Start the cursor, the debounce window and the row memory over. */
    void ResetCursor(int slot);

    /** How long a row's value may sit on screen marked as just changed. */
    static constexpr int64_t ChangedMs = 150;

    MenuSession& _session;
    Translations& _translations;

    /** PerSlot clears a slot's stack when it changes hands. */
    PerSlot<PlayerMenuState> _states;
    MenuCursor _cursor;
    /** Commits held back while a row is being stepped. */
    PendingCommit _pending;
};

}  // namespace VoltMod
