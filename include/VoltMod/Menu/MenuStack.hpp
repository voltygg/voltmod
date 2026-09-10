#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuState.hpp>
#include <VoltMod/Menu/PendingCommit.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief One player's open menus, and the row behaviour every surface shares.
 *
 * The half of a menu session that has nothing to do with how it is drawn: the stack, the
 * breadcrumb, how a row describes itself, what activating a row does, and how a stepped value is
 * held back so a burst of presses is one action. @ref CenterHtmlMenu draws this as center HTML; a
 * plugin drawing its own Panorama screen holds one too, so the same @ref Menu behaves the same
 * way whichever surface a player is on.
 *
 * What stays outside: the cursor, page shape, key or click routing, freezing, and prompts. Those
 * differ per surface, and a surface that has no cursor should not be made to carry one.
 *
 * SDK-free. The delay arrives as a @ref PendingCommit::Timer, which is `Scheduler::Delay` in the
 * framework and a hand-fired one in the tests.
 */
class MenuStack
{
public:
    /** @p surface is what a row's Activate callback is handed: the surface owning this stack.
     *  Both references and the timer's target must outlive this instance. */
    MenuStack(MenuSurface& surface, Translations& translations, PendingCommit::Timer timer);

    /** Drop a slot's stack when it changes hands. @p slots must outlive this. */
    void BindReset(SlotEvents& slots);

    /** Top menu, or null when none is open. */
    [[nodiscard]] Menu* Current(int slot);

    /** Bottom menu - the one the session was opened with - or null. */
    [[nodiscard]] Menu* Root(int slot);

    [[nodiscard]] int Depth(int slot) const;
    [[nodiscard]] bool IsOpen(int slot) const;
    [[nodiscard]] bool AnyOpen() const;

    /** Parent menu titles joined. Valid until the stack changes. */
    [[nodiscard]] std::string_view Breadcrumb(int slot) const;

    void Push(int slot, std::shared_ptr<Menu> menu);

    /** Pop the top menu, applying whatever a stepped row was left showing. True when the stack is
     *  now empty, which is the caller's cue to unfreeze and take the menu off screen. */
    bool Pop(int slot);

    /** Clear the whole stack, applying whatever a stepped row was left showing. */
    void Clear(int slot);

    /** Unwind to the root without closing the session, for a surface where entering a branch is a
     *  jump rather than a push. Does nothing when nothing is open. */
    void Rewind(int slot);

    /** Row @p index as it describes itself, with @ref MenuRow::Pending and @ref MenuRow::Changed
     *  filled in and a Toggle's on/off word spelled. An index with no row behind it describes as
     *  an inert, unselectable line. */
    [[nodiscard]] MenuRow Describe(int slot, int index);

    /** Run row @p index, as if it had been selected and confirmed. Ignores rows that are disabled,
     *  unselectable, or out of range.
     *
     *  Runs a commit held for another row first, and cancels one held for *this* row: a row whose
     *  activation is its own commit would otherwise apply the value twice. */
    void Activate(int slot, int index);

    /** Nudge row @p index's value by @p direction (-1 or +1). True when the row consumed it, which
     *  is what tells a keyboard surface to page instead. A row with a @ref MenuItem::Commit is
     *  stepped, not applied: the commit is held so a burst of presses runs one action. */
    bool Step(int slot, int index, int direction);

    /** Apply whatever a stepped row was left showing, and forget it. Every way out of a row that
     *  is not another step goes through here. */
    void RunPending(int slot);

    /** True while @p slot is holding a stepped value for row @p index. */
    [[nodiscard]] bool IsPending(int slot, int index) const;

    /** A held commit was applied. A surface that redraws every frame can ignore this; one that
     *  draws on demand needs it, because the commit lands on a timer rather than on a press. */
    Event<int /*slot*/> Committed;

private:
    /** How long a stepped value waits before it is applied. */
    static constexpr int64_t ChangedMs = 150;

    struct State
    {
        std::vector<std::shared_ptr<Menu>> Menus;
        /** One entry per row of the current menu, rebuilt when the menu changes. */
        std::vector<MenuRowMemory> Rows;
        std::string Breadcrumb;
    };

    /** Re-derive what depends on which menu is on top. */
    void Rebuild(int slot);

    MenuSurface& _surface;
    Translations& _translations;
    PerSlot<State> _states;
    PendingCommit _pending;
};

}  // namespace VoltMod
