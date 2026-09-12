#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Subscriptions.hpp>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using VoltMod::Event;
using VoltMod::Subscription;
using VoltMod::Subscriptions;

/** A Subscription whose release appends @p id to @p released. */
static Subscription Releasing(std::vector<int>& released, int id)
{
    return Subscription([&released, id] { released.push_back(id); });
}

TEST_CASE("Subscriptions release what they hold when destroyed, newest first")
{
    std::vector<int> released;

    {
        Subscriptions subs;
        subs.Add(Releasing(released, 1));
        subs.Add(Releasing(released, 2));
        subs.Add(Releasing(released, 3));

        CHECK_FALSE(subs.Empty());
        CHECK(released.empty());
    }

    CHECK(released == std::vector<int>{3, 2, 1});
}

TEST_CASE("Clear releases everything and leaves it empty")
{
    std::vector<int> released;
    Subscriptions subs;

    subs.Add(Releasing(released, 1));
    subs.Add(Releasing(released, 2));
    subs.Clear();

    CHECK(released == std::vector<int>{2, 1});
    CHECK(subs.Empty());

    subs.Clear();
    CHECK(released.size() == 2);
}

TEST_CASE("A fresh Subscriptions is empty and clearing it does nothing")
{
    Subscriptions subs;

    CHECK(subs.Empty());
    subs.Clear();
    CHECK(subs.Empty());
}

TEST_CASE("Subscriptions are what keep a subscribed handler alive")
{
    Event<int> event;
    std::vector<int> seen;

    {
        Subscriptions subs;
        subs.Add(event += [&](int value) { seen.push_back(value); });

        event.Raise(1);
        CHECK(seen == std::vector<int>{1});
    }

    event.Raise(2);
    CHECK(seen == std::vector<int>{1});
    CHECK(event.Empty());
}

TEST_CASE("Moving Subscriptions moves what they hold, releasing nothing")
{
    std::vector<int> released;

    {
        Subscriptions subs;
        subs.Add(Releasing(released, 1));

        Subscriptions moved = std::move(subs);
        CHECK(released.empty());
        CHECK_FALSE(moved.Empty());
    }

    CHECK(released == std::vector<int>{1});
}

TEST_CASE("Assigning over Subscriptions releases what they held")
{
    std::vector<int> released;
    Subscriptions subs;
    subs.Add(Releasing(released, 1));

    Subscriptions replacement;
    replacement.Add(Releasing(released, 2));
    subs = std::move(replacement);

    CHECK(released == std::vector<int>{1});
}
