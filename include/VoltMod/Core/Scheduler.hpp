#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <cstdint>
#include <functional>

namespace VoltMod
{

/**
 * @brief Tick-based task scheduler for one-shot delays and repeating timers.
 * Driven by `OnGameFrame()` (called every server tick from the plugin's GameFrame hook).
 * All callbacks execute on the game thread; no synchronization required.
 */
class Scheduler
{
public:
    Scheduler() = default;

    /** Run `callback` once after `delayMs` milliseconds. Returns a cancellation handle. */
    uint64_t Delay(int64_t delayMs, std::function<void()> callback);

    /** Run `callback` every `intervalMs` milliseconds, until the subscription is dropped. */
    [[nodiscard]] Subscription Repeat(int64_t intervalMs, std::function<void()> callback);

    /** Run `callback` on the very next game frame. */
    uint64_t NextTick(std::function<void()> callback);

    /** Run `callback` every game frame, until the subscription is dropped (e.g. a completion pump). */
    [[nodiscard]] Subscription EveryFrame(std::function<void()> callback);

    /** Cancel a timer by handle. Safe to call with an unknown id. */
    void Cancel(uint64_t id);

    /** Drive the scheduler (call from your `GameFrame` hook or via `VoltMod::OnGameFrame()`). */
    void OnGameFrame();

private:
    struct Timer
    {
        int64_t NextFireTime;
        int64_t Interval;
        std::function<void()> Callback;
    };

    int64_t GetCurrentTimeMs() const;

    CallbackRegistry<Timer> _timers;
};

}  // namespace VoltMod
