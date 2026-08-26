#include <VoltMod/Core/Scheduler.hpp>
#include <chrono>
#include <vector>

namespace VoltMod
{

int64_t Scheduler::GetCurrentTimeMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

uint64_t Scheduler::Delay(int64_t delayMs, std::function<void()> callback)
{
    return _timers.Add({GetCurrentTimeMs() + delayMs, 0, std::move(callback)});
}

Subscription Scheduler::Repeat(int64_t intervalMs, std::function<void()> callback)
{
    return _timers.AddOwned({GetCurrentTimeMs() + intervalMs, intervalMs, std::move(callback)});
}

uint64_t Scheduler::NextTick(std::function<void()> callback)
{
    return Delay(0, std::move(callback));
}

Subscription Scheduler::EveryFrame(std::function<void()> callback)
{
    // Interval -1 is the every-frame sentinel: OnGameFrame refires it each frame instead of
    // erasing it (interval 0 = one-shot) or waiting an interval (> 0).
    return _timers.AddOwned({0, -1, std::move(callback)});
}

void Scheduler::Cancel(uint64_t id)
{
    _timers.Remove(id);
}

void Scheduler::OnGameFrame()
{
    if (_timers.Empty())
        return;

    int64_t now = GetCurrentTimeMs();

    // Snapshot the IDs to fire instead of iterating live: callbacks may Cancel() other timers
    // (or themselves) or schedule new ones, which mutates the registry mid-loop.
    std::vector<uint64_t> toFire;
    for (const auto& [id, t] : _timers.Items())
    {
        if (now >= t.NextFireTime)
            toFire.push_back(id);
    }

    for (uint64_t id : toFire)
    {
        // Re-find by ID - the timer may have been cancelled by an earlier callback in this batch.
        Timer* timer = _timers.Find(id);
        if (!timer)
            continue;

        // Copy out before invoking - the callback can mutate the registry (invalidating pointers).
        int64_t interval = timer->Interval;
        auto callback = timer->Callback;

        if (callback)
            callback();

        // Re-find again: the callback may have cancelled this timer.
        timer = _timers.Find(id);
        if (!timer)
            continue;

        if (interval > 0)
            timer->NextFireTime = now + interval;
        else if (interval < 0)
            timer->NextFireTime = now;  // every-frame sentinel: due again next frame
        else
            _timers.Remove(id);
    }
}

}  // namespace VoltMod
