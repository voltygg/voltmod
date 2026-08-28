#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>

namespace VoltMod
{

/**
 * @brief Tick-based task scheduler for one-shot delays and repeating timers.
 *
 * Driven by `OnGameFrame()` (called every server tick from the plugin's GameFrame hook).
 * All callbacks execute on the game thread; no synchronization required.
 *
 * Every registration returns a @ref Subscription that cancels the timer when it drops - including
 * the one-shots, so a delayed callback can never outlive the state it captured. Letting the
 * subscription go before the timer fires cancels it; keeping it after a one-shot has fired is
 * harmless.
 */
class Scheduler
{
public:
    Scheduler() = default;

    /** Run @p callback once after @p delayMs milliseconds. */
    [[nodiscard]] Subscription Delay(int64_t delayMs, std::function<void()> callback);

    /** Run @p callback every @p intervalMs milliseconds. */
    [[nodiscard]] Subscription Repeat(int64_t intervalMs, std::function<void()> callback);

    /** Run @p callback on the very next game frame. */
    [[nodiscard]] Subscription NextTick(std::function<void()> callback);

    /** Run @p callback every game frame (e.g. per-frame completion delivery). */
    [[nodiscard]] Subscription EveryFrame(std::function<void()> callback);

    /** Drive the scheduler (call from your `GameFrame` hook or via `VoltMod::OnGameFrame()`). */
    void OnGameFrame();

private:
    struct Timer
    {
        int64_t NextFireTime;
        int64_t Interval;
        std::function<void()> Callback;
        uint64_t Id;  // needed to re-find the entry after the callback ran; see OnGameFrame
    };

    int64_t GetCurrentTimeMs() const;
    Subscription AddTimer(int64_t nextFireTime, int64_t interval, std::function<void()> callback);

    CallbackRegistry<Timer> _timers;
};

}  // namespace VoltMod
