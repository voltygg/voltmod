#pragma once

#include <algorithm>

namespace VoltMod::Core
{

/**
 * @brief Accumulator whose value drains linearly over time - the building block
 * for "suspicion" style scores where evidence should fade unless it repeats.
 *
 * Time is caller-supplied (seconds, any monotonic origin) so the class stays
 * clock-free and testable; pass the same time source to Add and Value.
 */
class DecayingScore
{
public:
    DecayingScore() = default;
    explicit DecayingScore(float decayPerSec) : _decayPerSec(decayPerSec) {}

    void SetDecayRate(float perSec) { _decayPerSec = perSec; }

    void Add(float amount, double now)
    {
        _value = std::max(0.0f, DecayedValue(now) + amount);
        _lastUpdate = now;
    }

    [[nodiscard]] float Value(double now) const { return DecayedValue(now); }

    void Reset()
    {
        _value = 0.0f;
        _lastUpdate = 0.0;
    }

private:
    [[nodiscard]] float DecayedValue(double now) const
    {
        double elapsed = std::max(0.0, now - _lastUpdate);
        return std::max(0.0f, _value - static_cast<float>(elapsed) * _decayPerSec);
    }

    float _value = 0.0f;
    double _lastUpdate = 0.0;
    float _decayPerSec = 0.0f;
};

}  // namespace VoltMod::Core
