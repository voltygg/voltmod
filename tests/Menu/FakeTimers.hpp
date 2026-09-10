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
 * Callbacks wait here until @ref Elapse runs them, which is the delay passing; dropping the
 * subscription the timer returned forgets one, which is the timer being stopped. Held behind a
 * shared_ptr so a subscription released after this object is gone still has somewhere to write.
 */
class FakeTimers
{
public:
    /** The @ref PendingCommit::Timer to construct the subject with. */
    VoltMod::PendingCommit::Timer Bind()
    {
        auto running = _running;
        auto next = _next;
        return [running, next](int64_t delayMs, std::function<void()> callback) {
            const uint64_t id = (*next)++;
            (*running)[id] = {delayMs, std::move(callback)};
            return VoltMod::Subscription([running, id] { running->erase(id); });
        };
    }

    /** Run every waiting callback, as the scheduler would once its delay had passed. */
    void Elapse()
    {
        auto due = *_running;
        _running->clear();
        for (auto& [id, timer] : due)
            timer.Callback();
    }

    /** How many commits are waiting on a timer. */
    [[nodiscard]] int Running() const { return static_cast<int>(_running->size()); }

    /** The delay the most recent timer asked for. */
    [[nodiscard]] int64_t LastDelay() const { return _running->empty() ? -1 : _running->rbegin()->second.DelayMs; }

private:
    struct Timer
    {
        int64_t DelayMs = 0;
        std::function<void()> Callback;
    };

    std::shared_ptr<std::map<uint64_t, Timer>> _running = std::make_shared<std::map<uint64_t, Timer>>();
    std::shared_ptr<uint64_t> _next = std::make_shared<uint64_t>(1);
};

}  // namespace VoltModTests
