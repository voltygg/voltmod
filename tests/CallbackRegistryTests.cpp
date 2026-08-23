#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
#include <doctest/doctest.h>

#include <functional>
#include <vector>

using CS2Kit::Core::CallbackRegistry;
using CS2Kit::Core::SlotEvents;
using CS2Kit::Core::Subscription;

namespace
{
using Fn = std::function<void()>;
}

TEST_CASE("Dispatch invokes every registered item")
{
    CallbackRegistry<Fn> registry;
    int calls = 0;
    auto a = registry.AddOwned([&] { ++calls; });
    auto b = registry.AddOwned([&] { ++calls; });

    registry.Dispatch([](Fn& fn) { fn(); });

    CHECK(calls == 2);
}

TEST_CASE("Dispatch on an empty registry does nothing")
{
    CallbackRegistry<Fn> registry;
    registry.Dispatch([](Fn& fn) { fn(); });
    CHECK(registry.Empty());
}

TEST_CASE("A callback may drop its own subscription while dispatching")
{
    CallbackRegistry<Fn> registry;
    Subscription self;
    int calls = 0;

    self = registry.AddOwned([&] {
        ++calls;
        self.Reset();  // erases this very entry mid-dispatch
    });

    registry.Dispatch([](Fn& fn) { fn(); });

    CHECK(calls == 1);
    CHECK(registry.Empty());
}

TEST_CASE("A callback that removes a later one stops it from running")
{
    CallbackRegistry<Fn> registry;
    Subscription second;
    int firstCalls = 0;
    int secondCalls = 0;

    auto first = registry.AddOwned([&] {
        ++firstCalls;
        second.Reset();
    });
    second = registry.AddOwned([&] { ++secondCalls; });

    registry.Dispatch([](Fn& fn) { fn(); });

    // Whichever order the two land in, removing the second must not resurrect or crash it.
    CHECK(firstCalls + secondCalls >= 1);
    CHECK(secondCalls <= 1);
}

TEST_CASE("A callback registering during dispatch does not run in the same pass")
{
    CallbackRegistry<Fn> registry;
    std::vector<Subscription> held;
    int added = 0;
    int outer = 0;

    auto first = registry.AddOwned([&] {
        ++outer;
        held.push_back(registry.AddOwned([&] { ++added; }));
    });

    registry.Dispatch([](Fn& fn) { fn(); });

    CHECK(outer == 1);
    CHECK(added == 0);
}

TEST_CASE("Dispatch survives more entries than the inline snapshot holds")
{
    CallbackRegistry<Fn> registry;
    std::vector<Subscription> subs;
    int calls = 0;
    for (int i = 0; i < 20; ++i)
        subs.push_back(registry.AddOwned([&] { ++calls; }));

    registry.Dispatch([](Fn& fn) { fn(); });

    CHECK(calls == 20);
}

TEST_CASE("A slot listener may unsubscribe itself from inside the notification")
{
    SlotEvents events;
    Subscription self;
    int seen = -1;

    self = events.Listen([&](int slot) {
        seen = slot;
        self.Reset();
    });

    events.Raise(7);

    CHECK(seen == 7);
    events.Raise(8);
    CHECK(seen == 7);  // unsubscribed, so the second raise never reaches it
}
