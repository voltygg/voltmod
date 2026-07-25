#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>

namespace CS2Kit::Utils
{

/**
 * @brief Rate limiter for repeated events, keyed by whatever identifies the actor.
 *
 * @ref TryAcquire succeeds at most once per interval for a given key; the first call for a key
 * always succeeds. The default interval is fixed at construction; the overloads taking one per
 * call suit limits that live in reloadable config.
 *
 * Pick the key by what must survive: slot-keyed state resets when a player rejoins into another
 * slot, SteamID-keyed state does not. Use a std::pair key for a per-actor-per-subject limit
 * ("this reporter already reported that player").
 */
template <class TKey, class TMap = std::unordered_map<TKey, int64_t>>
class Throttle
{
public:
    Throttle() = default;
    explicit Throttle(int64_t intervalSec) : _intervalSec(intervalSec) {}

    /** True (recording @p nowSec) when at least @p intervalSec has passed since the last acquire.
     *  An interval of 0 or less always succeeds. */
    bool TryAcquire(const TKey& key, int64_t nowSec, int64_t intervalSec)
    {
        if (RemainingSec(key, nowSec, intervalSec) > 0)
            return false;
        _lastAt[key] = nowSec;
        return true;
    }

    /** @ref TryAcquire against the interval given at construction. */
    bool TryAcquire(const TKey& key, int64_t nowSec) { return TryAcquire(key, nowSec, _intervalSec); }

    /** Seconds until @p key may acquire again, or 0 when it may acquire now. A backwards clock
     *  jump reads as "may acquire" rather than as a wait longer than the interval. */
    int64_t RemainingSec(const TKey& key, int64_t nowSec, int64_t intervalSec) const
    {
        if (intervalSec <= 0)
            return 0;
        auto it = _lastAt.find(key);
        if (it == _lastAt.end())
            return 0;
        const int64_t elapsed = nowSec - it->second;
        if (elapsed < 0 || elapsed >= intervalSec)
            return 0;
        return intervalSec - elapsed;
    }

    int64_t RemainingSec(const TKey& key, int64_t nowSec) const { return RemainingSec(key, nowSec, _intervalSec); }

    /** Record an acquire without asking - for callers that gate on their own combined verdict. */
    void Acquire(const TKey& key, int64_t nowSec) { _lastAt[key] = nowSec; }

    /** Forget a key's history (e.g. on disconnect, or to refund a failed action) so its next
     *  acquire succeeds immediately. */
    void Reset(const TKey& key) { _lastAt.erase(key); }

    /** Drop keys idle for longer than @p horizonSec, so the map tracks recent actors rather than
     *  every actor ever seen. */
    void Prune(int64_t nowSec, int64_t horizonSec)
    {
        std::erase_if(_lastAt, [&](const auto& entry) { return nowSec - entry.second > horizonSec; });
    }

private:
    int64_t _intervalSec = 0;
    TMap _lastAt;
};

/** Per-slot limiter for notices triggered by spammy hooks (per-keypress voice callbacks,
 *  per-message chat handlers). Resets when a player rejoins into a different slot. */
using SlotThrottle = Throttle<int>;

/** Key type is not hashable by default, so pair-keyed throttles use an ordered map. */
template <class TFirst, class TSecond>
using PairThrottle = Throttle<std::pair<TFirst, TSecond>, std::map<std::pair<TFirst, TSecond>, int64_t>>;

}  // namespace CS2Kit::Utils
