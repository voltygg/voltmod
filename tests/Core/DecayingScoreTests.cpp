#include <VoltMod/Core/DecayingScore.hpp>
#include <cmath>
#include <doctest/doctest.h>

using VoltMod::Core::DecayingScore;

namespace
{
bool Near(float a, float b, float eps = 0.001f)
{
    return std::fabs(a - b) < eps;
}
}  // namespace

TEST_CASE("DecayingScore accumulates and decays linearly")
{
    DecayingScore score(2.0f);  // 2 points/sec drain

    score.Add(10.0f, 100.0);
    CHECK(Near(score.Value(100.0), 10.0f));
    CHECK(Near(score.Value(102.0), 6.0f));
    CHECK(Near(score.Value(105.0), 0.0f));  // never below zero

    score.Add(5.0f, 102.0);  // decayed to 6 first, then +5
    CHECK(Near(score.Value(102.0), 11.0f));
}

TEST_CASE("DecayingScore with zero rate holds value")
{
    DecayingScore score;
    score.Add(7.0f, 1.0);
    CHECK(Near(score.Value(1000.0), 7.0f));
}

TEST_CASE("DecayingScore reset and non-monotonic time")
{
    DecayingScore score(1.0f);
    score.Add(4.0f, 50.0);
    CHECK(Near(score.Value(49.0), 4.0f));  // clock going backwards never inflates

    score.Reset();
    CHECK(Near(score.Value(60.0), 0.0f));
}
