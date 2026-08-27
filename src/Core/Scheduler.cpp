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
    // OnGameFrame runs against a copy of the timer, so the entry has to carry its own handle to be
    // re-findable afterwards. Add just stored it, so this lookup cannot fail.
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
    // Interval -1 is the every-frame sentinel: OnGameFrame refires it each frame instead of
    // erasing it (interval 0 = one-shot) or waiting an interval (> 0).
    return AddTimer(0, -1, std::move(callback));
}

void Scheduler::OnGameFrame()
{
    const int64_t now = GetCurrentTimeMs();

    // DispatchIf filters on the stored timer and re-checks it before each call, so a callback is
    // free to cancel other timers (or itself) and to schedule new ones - the latter start next
    // frame rather than joining this batch.
    _timers.DispatchIf([now](const Timer& timer) { return now >= timer.NextFireTime; },
                       [this, now](Timer& timer) {
                           if (timer.Callback)
                               timer.Callback();

                           // The callback may have cancelled this timer, directly or by dropping
                           // the subscription that owns it.
                           Timer* stored = _timers.Find(timer.Id);
                           if (!stored)
                               return;

                           if (timer.Interval > 0)
                               stored->NextFireTime = now + timer.Interval;
                           else if (timer.Interval < 0)
                               stored->NextFireTime = now;  // every-frame sentinel: due again next frame
                           else
                               _timers.Remove(timer.Id);
                       });
}

}  // namespace VoltMod
