#include <VoltMod/Core/Time/DecayingScore.hpp>
#include <doctest/doctest.h>

using VoltMod::DecayingScore;

TEST_CASE("DecayingScore adds weight and reports it")
{
    DecayingScore score(10.0);
    CHECK(score.Add(100.0) == doctest::Approx(1.0));
    CHECK(score.Add(100.0, 3.0) == doctest::Approx(4.0));
    CHECK(score.Value(100.0) == doctest::Approx(4.0));
}

TEST_CASE("DecayingScore halves once per half-life")
{
    DecayingScore score(10.0);
    score.Add(0.0, 8.0);
    CHECK(score.Value(10.0) == doctest::Approx(4.0));
    CHECK(score.Value(20.0) == doctest::Approx(2.0));
    CHECK(score.Value(30.0) == doctest::Approx(1.0));
}

TEST_CASE("DecayingScore ages what it holds before adding more")
{
    DecayingScore score(10.0);
    score.Add(0.0, 8.0);
    // The 8 has halved to 4 by now, so the running total is 6, not 10.
    CHECK(score.Add(10.0, 2.0) == doctest::Approx(6.0));
}

TEST_CASE("DecayingScore never reaches zero but approaches it")
{
    DecayingScore score(1.0);
    score.Add(0.0, 1.0);
    CHECK(score.Value(60.0) < 1e-15);
    CHECK(score.Value(60.0) > 0.0);
}

TEST_CASE("DecayingScore with a half-life of zero or less never decays")
{
    DecayingScore latched;
    latched.Add(0.0, 5.0);
    CHECK(latched.Value(1e9) == doctest::Approx(5.0));

    latched.SetHalfLife(-1.0);
    CHECK(latched.Value(1e9) == doctest::Approx(5.0));
}

TEST_CASE("DecayingScore treats a backwards clock as no time passed")
{
    DecayingScore score(10.0);
    score.Add(100.0, 4.0);

    // A map restart can hand us a time before the stamp; that must not amplify the score.
    CHECK(score.Value(0.0) == doctest::Approx(4.0));
    CHECK(score.Add(0.0, 1.0) == doctest::Approx(5.0));
}

TEST_CASE("DecayingScore Value does not move the stamp")
{
    DecayingScore score(10.0);
    score.Add(0.0, 8.0);

    // Reading at +10 must not re-base the decay: the value at +20 is still a quarter.
    CHECK(score.Value(10.0) == doctest::Approx(4.0));
    CHECK(score.Value(20.0) == doctest::Approx(2.0));
}

TEST_CASE("DecayingScore clears on demand")
{
    DecayingScore score(10.0);
    score.Add(0.0, 4.0);
    score.Clear();
    CHECK(score.Value(0.0) == doctest::Approx(0.0));
    CHECK(score.Add(0.0) == doctest::Approx(1.0));
}

TEST_CASE("DecayingScore takes its half-life after construction")
{
    // For members that cannot pass a constructor argument.
    DecayingScore score;
    score.SetHalfLife(10.0);
    score.Add(0.0, 8.0);
    CHECK(score.Value(10.0) == doctest::Approx(4.0));
}
