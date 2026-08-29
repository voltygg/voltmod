#include "Menu/PendingCommit.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <functional>
#include <map>
#include <memory>
#include <utility>

using VoltMod::PendingCommit;
using VoltMod::SlotEvents;
using VoltMod::Subscription;

namespace VoltModTests
{

/**
 * @brief The scheduler seam a `PendingCommit` takes, with the clock in the test's hands.
 *
 * Armed callbacks sit here until @ref Elapse runs them, which is the delay passing; dropping the
 * subscription the arm returned forgets one, which is the timer being cancelled. Held behind a
 * shared_ptr so a subscription released after this object is gone still has somewhere to write.
 */
class FakeTimers
{
public:
    /** The @ref PendingCommit::Timer to construct the subject with. */
    PendingCommit::Timer Bind()
    {
        auto armed = _armed;
        auto next = _next;
        return [armed, next](int64_t delayMs, std::function<void()> callback) {
            const uint64_t id = (*next)++;
            (*armed)[id] = {delayMs, std::move(callback)};
            return Subscription([armed, id] { armed->erase(id); });
        };
    }

    /** Run every armed callback, as the scheduler would once its delay had passed. */
    void Elapse()
    {
        auto due = *_armed;
        _armed->clear();
        for (auto& [id, timer] : due)
            timer.Callback();
    }

    /** How many commits are waiting on a timer. */
    [[nodiscard]] int Armed() const { return static_cast<int>(_armed->size()); }

    /** The delay the most recent arm asked for. */
    [[nodiscard]] int64_t LastDelay() const { return _armed->empty() ? -1 : _armed->rbegin()->second.DelayMs; }

private:
    struct Timer
    {
        int64_t DelayMs = 0;
        std::function<void()> Callback;
    };

    std::shared_ptr<std::map<uint64_t, Timer>> _armed = std::make_shared<std::map<uint64_t, Timer>>();
    std::shared_ptr<uint64_t> _next = std::make_shared<uint64_t>(1);
};

}  // namespace VoltModTests

using VoltModTests::FakeTimers;

TEST_CASE("PendingCommit: a stepped row waits out the delay before it commits")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int commits = 0;
    pending.Arm(0, 2, [&] { ++commits; });

    CHECK(commits == 0);
    CHECK(timers.Armed() == 1);
    CHECK(timers.LastDelay() == PendingCommit::DelayMs);
    CHECK(pending.IsPending(0, 2));
    CHECK(pending.Index(0) == 2);

    timers.Elapse();
    CHECK(commits == 1);
    CHECK_FALSE(pending.IsPending(0, 2));
}

TEST_CASE("PendingCommit: a burst of steps on one row is one commit")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int commits = 0;
    for (int i = 0; i < 5; ++i)
        pending.Arm(0, 1, [&] { ++commits; });

    // Each arm replaced the last, so one timer is live and nothing has run yet.
    CHECK(commits == 0);
    CHECK(timers.Armed() == 1);

    timers.Elapse();
    CHECK(commits == 1);
}

TEST_CASE("PendingCommit: stepping another row commits the one before it")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int first = 0;
    int second = 0;
    pending.Arm(0, 1, [&] { ++first; });
    pending.Arm(0, 2, [&] { ++second; });

    CHECK(first == 1);
    CHECK(second == 0);
    CHECK(pending.Index(0) == 2);
    CHECK(timers.Armed() == 1);
}

TEST_CASE("PendingCommit: running it applies the value now and drops the timer")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int commits = 0;
    pending.Arm(0, 0, [&] { ++commits; });
    pending.Run(0);

    CHECK(commits == 1);
    CHECK(timers.Armed() == 0);
    CHECK(pending.Index(0) == -1);

    // Nothing is left to fire, so the delay passing changes nothing.
    timers.Elapse();
    CHECK(commits == 1);
}

TEST_CASE("PendingCommit: cancelling drops the value unrun")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int commits = 0;
    pending.Arm(0, 0, [&] { ++commits; });
    pending.Cancel(0);
    timers.Elapse();

    CHECK(commits == 0);
    CHECK(timers.Armed() == 0);
    CHECK(pending.Index(0) == -1);
}

TEST_CASE("PendingCommit: a slot changing hands cancels rather than commits")
{
    SlotEvents slots;
    FakeTimers timers;
    PendingCommit pending(timers.Bind());
    pending.BindReset(slots);

    int commits = 0;
    pending.Arm(3, 4, [&] { ++commits; });
    slots.Raise(3);
    timers.Elapse();

    // The player the value was picked for is gone; applying it now would act for whoever takes
    // the slot next.
    CHECK(commits == 0);
    CHECK(pending.Index(3) == -1);
}

TEST_CASE("PendingCommit: one player's pending value is not another's")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int first = 0;
    int second = 0;
    pending.Arm(0, 1, [&] { ++first; });
    pending.Arm(5, 1, [&] { ++second; });

    pending.Run(0);
    CHECK(first == 1);
    CHECK(second == 0);
    CHECK(pending.IsPending(5, 1));

    pending.RunAll();
    CHECK(first == 1);
    CHECK(second == 1);
}

TEST_CASE("PendingCommit: running an empty slot does nothing")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    pending.Run(0);
    pending.Cancel(0);
    pending.RunAll();

    CHECK(pending.Index(0) == -1);
    CHECK_FALSE(pending.IsPending(0, -1));
}

TEST_CASE("PendingCommit: a commit that arms the next value is not run twice")
{
    FakeTimers timers;
    PendingCommit pending(timers.Bind());

    int commits = 0;
    int rearmed = 0;
    pending.Arm(0, 1, [&] {
        ++commits;
        // A commit is free to step the row again - the entry it is replacing is already out.
        pending.Arm(0, 1, [&] { ++rearmed; });
    });

    timers.Elapse();
    CHECK(commits == 1);
    CHECK(rearmed == 0);
    CHECK(pending.IsPending(0, 1));

    timers.Elapse();
    CHECK(commits == 1);
    CHECK(rearmed == 1);
}
