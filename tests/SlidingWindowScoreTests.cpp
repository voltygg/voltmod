#include <CS2Kit/Utils/SlidingWindowScore.hpp>
#include <doctest/doctest.h>

using CS2Kit::Utils::SlidingWindowScore;

TEST_CASE("SlidingWindowScore counts unweighted events inside the window")
{
    SlidingWindowScore window(10.0);
    CHECK(window.Add(100.0) == 1);
    CHECK(window.Add(105.0) == 2);
    CHECK(window.Value(105.0) == 2);
}

TEST_CASE("SlidingWindowScore drops entries once the window has elapsed")
{
    SlidingWindowScore window(10.0);
    window.Add(100.0);
    window.Add(105.0);

    // The boundary is exclusive: exactly one window later no longer counts.
    CHECK(window.Add(110.0) == 2);
    CHECK(window.Add(120.0) == 1);
}

TEST_CASE("SlidingWindowScore sums weights")
{
    SlidingWindowScore window(60.0);
    CHECK(window.Add(0.0, 3) == 3);
    CHECK(window.Add(10.0, 2) == 5);
    CHECK(window.Value(59.0) == 5);
    CHECK(window.Value(65.0) == 2);
}

TEST_CASE("SlidingWindowScore clears on demand")
{
    SlidingWindowScore window(10.0);
    window.Add(0.0, 4);
    window.Clear();
    CHECK(window.Value(0.0) == 0);
    CHECK(window.Count() == 0);
    CHECK(window.Add(1.0) == 1);
}

TEST_CASE("SlidingWindowScore Count reports stored entries, which only Add prunes")
{
    SlidingWindowScore window(10.0);
    window.Add(0.0);
    window.Add(1.0);
    REQUIRE(window.Count() == 2);

    // Value is time-aware; Count deliberately is not.
    CHECK(window.Value(100.0) == 0);
    CHECK(window.Count() == 2);

    window.Add(100.0);
    CHECK(window.Count() == 1);
}

TEST_CASE("SlidingWindowScore with the default window keeps nothing")
{
    SlidingWindowScore window;
    CHECK(window.Add(0.0) == 0);
    CHECK(window.Value(0.0) == 0);
}

TEST_CASE("SlidingWindowScore takes its window after construction")
{
    // For members that cannot pass a constructor argument.
    SlidingWindowScore window;
    window.SetWindow(5.0);
    CHECK(window.Add(0.0) == 1);
    CHECK(window.Add(1.0) == 2);
    CHECK(window.Add(10.0) == 1);
}
