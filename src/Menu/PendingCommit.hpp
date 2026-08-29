#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>

namespace VoltMod
{

/**
 * @brief One held-back row commit per player: what a stepped row was left showing, applied once
 * the stepping stops.
 *
 * A stepped row shows its new value the moment A/D or a stepper is pressed, but running its
 * action on every press would make five taps five actions and five broadcasts. @ref Arm holds the
 * commit for @ref DelayMs and replaces whatever the slot was holding, so a burst commits once.
 *
 * Every other way out of the row runs what is held rather than dropping it - activating the row,
 * closing the menu, moving the cursor off it - because a value the player picked and watched
 * appear is a value they asked for. A slot changing hands is the one case that cancels: nobody is
 * left to have asked for it.
 *
 * SDK-free. The delay arrives as a @ref Timer, which is `Scheduler::Delay` in the manager and a
 * hand-fired one in the tests, so the policy is unit-tested without an engine or a real clock.
 */
class PendingCommit
{
public:
    /** Run @p callback after @p delayMs; dropping the returned subscription cancels it. */
    using Timer = std::function<Subscription(int64_t delayMs, std::function<void()> callback)>;

    /** How long stepping is given to settle. Long enough to ride out a burst of presses, short
     *  enough that a player who steps once and looks away still sees the value land. */
    static constexpr int64_t DelayMs = 400;

    /** @p timer is stored and called on every @ref Arm. */
    explicit PendingCommit(Timer timer);

    /** Drop a slot's pending commit unrun when the slot changes hands. @p slots must outlive
     *  this. */
    void BindReset(SlotEvents& slots);

    /** Hold @p commit for row @p index of @p slot. Re-arming the same row restarts the delay;
     *  arming a different one applies what the previous row was holding first. */
    void Arm(int slot, int index, std::function<void()> commit);

    /** The row @p slot has a commit waiting for, or -1. */
    [[nodiscard]] int Index(int slot) const;

    /** True while @p slot's pending commit belongs to row @p index. */
    [[nodiscard]] bool IsPending(int slot, int index) const;

    /** Apply @p slot's pending commit now, if it has one, and cancel its timer. */
    void Run(int slot);

    /** Apply every player's pending commit, for a driver swap that ends every session at once. */
    void RunAll();

    /** Drop @p slot's pending commit unrun. */
    void Cancel(int slot);

private:
    struct Entry
    {
        int Index = -1;
        std::function<void()> Commit;
        /** Declared last: dropping it cancels the timer that would have run @ref Commit. */
        Subscription Timer;
    };

    PendingCommit::Timer _timer;
    PerSlot<Entry> _entries;
};

}  // namespace VoltMod
