#pragma once

#include <cmath>

namespace VoltMod
{

/**
 * @brief A score that loses half its weight every half-life - the building block for evidence
 * that should fade with age rather than expire on a window edge.
 *
 * Holds one value and one timestamp, so it costs the same whether it saw one event or a
 * thousand. Time is caller-supplied (seconds, any monotonic origin) so the class stays
 * clock-free and testable; pass the same time source to every call.
 *
 * A half-life of 0 or less never decays, which is how a score latches.
 */
class DecayingScore
{
public:
    DecayingScore() = default;
    explicit DecayingScore(double halfLifeSec) : _halfLifeSec(halfLifeSec) {}

    /** Takes effect from the next @ref Value; weight already accrued is not re-aged. */
    void SetHalfLife(double halfLifeSec) { _halfLifeSec = halfLifeSec; }
    double HalfLife() const { return _halfLifeSec; }

    /** Ages the score to @p now, adds @p weight, and returns the result. */
    double Add(double now, double weight = 1.0)
    {
        _value = Value(now) + weight;
        _stamp = now;
        return _value;
    }

    /** The score at @p now, leaving the stored value and stamp alone. */
    double Value(double now) const
    {
        const double elapsed = now - _stamp;
        // A clock that went backwards - map restart, skew - reads as no time passed, never as gain.
        if (_halfLifeSec <= 0.0 || elapsed <= 0.0)
        {
            return _value;
        }
        return _value * std::exp2(-elapsed / _halfLifeSec);
    }

    void Clear()
    {
        _value = 0.0;
        _stamp = 0.0;
    }

private:
    double _value = 0.0;
    double _stamp = 0.0;
    double _halfLifeSec = 0.0;
};

}  // namespace VoltMod
