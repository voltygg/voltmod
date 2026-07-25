#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <doctest/doctest.h>
#include <functional>

using CS2Kit::Core::CallbackRegistry;

TEST_CASE("CallbackRegistry Add returns increasing handles from 1")
{
    CallbackRegistry<std::function<void()>> reg;
    uint64_t a = reg.Add([] {});
    uint64_t b = reg.Add([] {});
    CHECK_EQ(a, 1u);
    CHECK_EQ(b, 2u);
    CHECK(reg.Remove(a));
    CHECK(!reg.Remove(a));  // already gone
}

TEST_CASE("Add with caller-supplied id shares one handle space across registries")
{
    // Mirrors MovementHook: several registries behind one RemoveListener(id). A shared
    // external counter must keep handles disjoint so removing one never hits another.
    CallbackRegistry<std::function<void()>> a;
    CallbackRegistry<std::function<void()>> b;
    uint64_t next = 1;

    uint64_t idA = a.Add([] {}, next++);
    uint64_t idB = b.Add([] {}, next++);
    CHECK_EQ(idA, 1u);
    CHECK_EQ(idB, 2u);

    // Removing idA from both registries (the RemoveListener pattern) must not touch b's entry.
    a.Remove(idA);
    b.Remove(idA);
    CHECK(a.Empty());
    CHECK(!b.Empty());  // idB survived; per-registry counters would both have issued 1 and collided

    a.Remove(idB);
    b.Remove(idB);
    CHECK(b.Empty());
}
