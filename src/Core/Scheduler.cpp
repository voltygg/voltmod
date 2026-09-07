#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Time.hpp>
#include <utility>

namespace VoltMod
{

int64_t Scheduler::GetCurrentTimeMs() const
{
    return Time::MonotonicMs();
}

Subscription Scheduler::AddTimer(int64_t nextFireTime, int64_t interval, std::function<void()> callback)
{
    const uint64_t id = _timers.Add({nextFireTime, interval, std::move(callback), 0});
    // Frame dispatch uses a copy, so each entry carries its handle for the follow-up lookup.
    _timers.Find(id)->Id = id;
    return Subscription([this, id] { _timers.Remove(id); });
}

Subscription Scheduler::Delay(int64_t delayMs, std::function<void()> callback)
{
    return AddTimer(GetCurrentTimeMs() + delayMs, 0, std::move(callback));
}

Subscription Scheduler::Repeat(int64_t intervalMs, std::function<void()> callback)
{
    return AddTimer(GetCurrentTimeMs() + intervalMs, intervalMs, std::move(callback));
}

Subscription Scheduler::NextTick(std::function<void()> callback)
{
    return Delay(0, std::move(callback));
}

Subscription Scheduler::EveryFrame(std::function<void()> callback)
{
    // -1 repeats every frame, 0 is one-shot, and positive values wait between calls.
    return AddTimer(0, -1, std::move(callback));
}

void Scheduler::OnGameFrame()
{
    const int64_t now = GetCurrentTimeMs();

    // Recheck each timer before invoking it. Callbacks may cancel timers; new timers start next frame.
    _timers.DispatchIf([now](const Timer& timer) { return now >= timer.NextFireTime; },
                       [this, now](Timer& timer) {
                           if (timer.Callback)
                               timer.Callback();

                           // The callback may have cancelled this timer.
                           Timer* stored = _timers.Find(timer.Id);
                           if (!stored)
                               return;

                           if (timer.Interval > 0)
                               stored->NextFireTime = now + timer.Interval;
                           else if (timer.Interval < 0)
                               stored->NextFireTime = now;  // repeat on the next frame
                           else
                               _timers.Remove(timer.Id);
                       });
}

}  // namespace VoltMod
