#include <CS2Kit/Core/SlotThrottle.hpp>
#include <cstdint>
#include <doctest/doctest.h>

using CS2Kit::Core::PairThrottle;
using CS2Kit::Core::SlotThrottle;

TEST_CASE("SlotThrottle: first acquire always succeeds")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(3, 1000));
}

TEST_CASE("SlotThrottle: re-acquire blocked inside interval, allowed after")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(3, 1000));
    CHECK(!throttle.TryAcquire(3, 1030));
    CHECK(!throttle.TryAcquire(3, 1059));
    CHECK(throttle.TryAcquire(3, 1060));
}

TEST_CASE("SlotThrottle: slots are independent")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(1, 1000));
    CHECK(throttle.TryAcquire(2, 1000));
    CHECK(!throttle.TryAcquire(1, 1010));
}

TEST_CASE("SlotThrottle: Reset re-arms a slot")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(5, 1000));
    CHECK(!throttle.TryAcquire(5, 1010));
    throttle.Reset(5);
    CHECK(throttle.TryAcquire(5, 1011));
}

TEST_CASE("Throttle: RemainingSec counts down and reaches zero")
{
    SlotThrottle throttle(60);
    CHECK(throttle.RemainingSec(3, 1000) == 0);  // never acquired
    CHECK(throttle.TryAcquire(3, 1000));
    CHECK(throttle.RemainingSec(3, 1000) == 60);
    CHECK(throttle.RemainingSec(3, 1030) == 30);
    CHECK(throttle.RemainingSec(3, 1060) == 0);
}

TEST_CASE("Throttle: a per-call interval overrides the constructed one")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(3, 1000, 10));
    CHECK(!throttle.TryAcquire(3, 1005, 10));
    CHECK(throttle.TryAcquire(3, 1010, 10));
}

TEST_CASE("Throttle: a non-positive interval never blocks")
{
    SlotThrottle throttle(0);
    CHECK(throttle.TryAcquire(3, 1000));
    CHECK(throttle.TryAcquire(3, 1000));
    CHECK(throttle.RemainingSec(3, 1000) == 0);
}

TEST_CASE("Throttle: a backwards clock jump reads as acquirable")
{
    SlotThrottle throttle(60);
    CHECK(throttle.TryAcquire(3, 5000));
    CHECK(throttle.RemainingSec(3, 1000) == 0);
    CHECK(throttle.TryAcquire(3, 1000));
}

TEST_CASE("Throttle: Prune drops only entries past the horizon")
{
    SlotThrottle throttle(60);
    throttle.Acquire(1, 1000);
    throttle.Acquire(2, 1900);
    throttle.Prune(2000, 500);
    CHECK(throttle.RemainingSec(1, 1900) == 0);   // pruned, so unrestricted
    CHECK(throttle.RemainingSec(2, 1900) == 60);  // kept
}

TEST_CASE("PairThrottle: each (actor, subject) pair is limited independently")
{
    PairThrottle<int64_t, int64_t> throttle(1800);
    CHECK(throttle.TryAcquire({7, 8}, 1000));
    CHECK(!throttle.TryAcquire({7, 8}, 1500));  // same pair, still inside the window
    CHECK(throttle.TryAcquire({7, 9}, 1500));   // same actor, different subject
    CHECK(throttle.TryAcquire({9, 8}, 1500));   // different actor, same subject
    CHECK(throttle.TryAcquire({7, 8}, 2800));   // window elapsed
}
