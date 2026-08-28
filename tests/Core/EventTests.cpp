#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <doctest/doctest.h>
#include <string>
#include <utility>
#include <vector>

using VoltMod::Event;
using VoltMod::SlotEvents;
using VoltMod::Subscription;

TEST_CASE("Every subscribed handler sees the raise")
{
    Event<int> event;
    int seen = 0;
    auto a = event += [&](int value) { seen += value; };
    auto b = event += [&](int value) { seen += value; };

    event.Raise(3);

    CHECK(event.Count() == 2);
    CHECK(seen == 6);
}

TEST_CASE("Dropping a subscription stops its handler")
{
    Event<int> event;
    int seen = 0;
    {
        auto sub = event += [&](int value) { seen = value; };
        event.Raise(1);
    }

    event.Raise(2);

    CHECK(seen == 1);
    CHECK(event.Empty());
}

TEST_CASE("An empty event raises to nobody")
{
    Event<> event;
    CHECK(event.Empty());
    CHECK(event.Count() == 0);
    event.Raise();
}

TEST_CASE("A handler may drop its own subscription while the event is raising")
{
    Event<int> event;
    Subscription self;
    int calls = 0;

    self = event += [&](int) {
        ++calls;
        self.Reset();
    };

    event.Raise(1);
    event.Raise(2);

    CHECK(calls == 1);
    CHECK(event.Empty());
}

TEST_CASE("A handler added during a raise first fires on the next one")
{
    Event<int> event;
    std::vector<Subscription> held;
    int outer = 0;
    int inner = 0;

    auto first = event += [&](int) {
        ++outer;
        if (held.empty())
            held.push_back(event += [&](int) { ++inner; });
    };

    event.Raise(1);
    CHECK(outer == 1);
    CHECK(inner == 0);  // added mid-raise, so it sits out this pass

    event.Raise(2);
    CHECK(outer == 2);
    CHECK(inner == 1);  // and fires from the next one on
}

TEST_CASE("Handlers receive reference arguments without copying them")
{
    Event<std::string&> event;
    auto sub = event += [](std::string& text) { text += "!"; };

    std::string text = "hi";
    event.Raise(text);

    CHECK(text == "hi!");
}

TEST_CASE("Lifecycle installs on the first subscriber and removes after the last")
{
    int installs = 0;
    int removals = 0;
    Event<int> event({.OnFirst =
                          [&] {
                              ++installs;
                              return true;
                          },
                      .OnLast = [&] { ++removals; }});

    CHECK(installs == 0);

    auto first = event += [](int) {};
    CHECK(installs == 1);
    CHECK(removals == 0);

    auto second = event += [](int) {};
    CHECK(installs == 1);  // only the first subscriber installs it

    second.Reset();
    CHECK(removals == 0);  // one handler is still listening

    first.Reset();
    CHECK(removals == 1);
}

TEST_CASE("A refused Lifecycle rejects the subscription and never runs OnLast")
{
    int removals = 0;
    Event<int> event({.OnFirst = [] { return false; }, .OnLast = [&] { ++removals; }});

    int calls = 0;
    auto sub = event += [&](int) { ++calls; };

    CHECK_FALSE(static_cast<bool>(sub));
    CHECK(event.Count() == 0);

    event.Raise(1);
    CHECK(calls == 0);

    sub.Reset();
    CHECK(removals == 0);
}

TEST_CASE("A refused Lifecycle is retried by the next subscriber")
{
    bool allow = false;
    int installs = 0;
    Event<int> event({.OnFirst =
                          [&] {
                              ++installs;
                              return allow;
                          },
                      .OnLast = [] {}});

    auto refused = event += [](int) {};
    CHECK(installs == 1);
    CHECK(event.Empty());

    allow = true;
    auto accepted = event += [](int) {};
    CHECK(installs == 2);
    CHECK(event.Count() == 1);
}

TEST_CASE("Moving a subscription moves the registration with it")
{
    Event<int> event;
    int calls = 0;

    Subscription moved;
    {
        auto original = event += [&](int) { ++calls; };
        moved = std::move(original);
        CHECK_FALSE(static_cast<bool>(original));
    }

    event.Raise(1);
    CHECK(calls == 1);

    moved.Reset();
    event.Raise(2);
    CHECK(calls == 1);
}

TEST_CASE("A move-assigned subscription releases the registration it held")
{
    Event<int> event;
    int first = 0;
    int second = 0;

    Subscription sub = event += [&](int) { ++first; };
    sub = event += [&](int) { ++second; };

    event.Raise(1);

    CHECK(first == 0);  // the first registration went away with the assignment
    CHECK(second == 1);
    CHECK(event.Count() == 1);
}

TEST_CASE("A slot handler may unsubscribe itself from inside the notification")
{
    SlotEvents slots;
    Subscription self;
    int seen = -1;

    self = slots.Changed += [&](int slot) {
        seen = slot;
        self.Reset();
    };

    slots.Raise(7);
    CHECK(seen == 7);

    slots.Raise(8);
    CHECK(seen == 7);  // unsubscribed, so the second raise never reaches it
}
