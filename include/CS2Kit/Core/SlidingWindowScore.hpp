#pragma once

#include <deque>

namespace CS2Kit::Core
{

/**
 * @brief Counts weighted events inside a fixed trailing window - the building block for
 * "N incidents within the last M seconds" thresholds.
 *
 * Time is caller-supplied (seconds, any monotonic origin) so the class stays clock-free and
 * testable; pass the same time source to every call. Prefer @ref DecayingScore when evidence
 * should fade smoothly instead of expiring on a hard boundary.
 */
class SlidingWindowScore
{
public:
    SlidingWindowScore() = default;
    explicit SlidingWindowScore(double windowSec) : _windowSec(windowSec) {}

    void SetWindow(double windowSec) { _windowSec = windowSec; }

    /** Drops expired entries, records @p weight at @p now, and returns the resulting total. */
    int Add(double now, int weight = 1)
    {
        while (!_entries.empty() && now - _entries.front().Time >= _windowSec)
            _entries.pop_front();
        _entries.push_back({.Time = now, .Weight = weight});
        return Value(now);
    }

    /** Total weight still inside the window. Expired entries are ignored, not dropped. */
    [[nodiscard]] int Value(double now) const
    {
        int total = 0;
        for (const Entry& entry : _entries)
            if (now - entry.Time < _windowSec)
                total += entry.Weight;
        return total;
    }

    /** Entries held right now, expired ones included - pruning only happens on @ref Add. Cheap
     *  enough for diagnostics that have no clock to hand; use @ref Value to decide anything. */
    [[nodiscard]] size_t Count() const { return _entries.size(); }

    void Clear() { _entries.clear(); }

private:
    struct Entry
    {
        double Time = 0.0;
        int Weight = 0;
    };

    std::deque<Entry> _entries;
    double _windowSec = 0.0;
};

}  // namespace CS2Kit::Core
