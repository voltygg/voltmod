#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Menu/PendingCommit.hpp>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <utility>

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
    VoltMod::PendingCommit::Timer Bind()
    {
        auto armed = _armed;
        auto next = _next;
        return [armed, next](int64_t delayMs, std::function<void()> callback) {
            const uint64_t id = (*next)++;
            (*armed)[id] = {delayMs, std::move(callback)};
            return VoltMod::Subscription([armed, id] { armed->erase(id); });
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
