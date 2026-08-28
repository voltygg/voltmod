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

TEST_CASE("A bag releases what it holds when it is destroyed, newest first")
{
    std::vector<int> released;

    {
        Subscriptions bag;
        bag.Add(Releasing(released, 1));
        bag.Add(Releasing(released, 2));
        bag.Add(Releasing(released, 3));

        CHECK_FALSE(bag.Empty());
        CHECK(released.empty());
    }

    CHECK(released == std::vector<int>{3, 2, 1});
}

TEST_CASE("Clear releases everything and leaves the bag empty")
{
    std::vector<int> released;
    Subscriptions bag;

    bag.Add(Releasing(released, 1));
    bag.Add(Releasing(released, 2));
    bag.Clear();

    CHECK(released == std::vector<int>{2, 1});
    CHECK(bag.Empty());

    bag.Clear();
    CHECK(released.size() == 2);
}

TEST_CASE("A fresh bag is empty and clearing it does nothing")
{
    Subscriptions bag;

    CHECK(bag.Empty());
    bag.Clear();
    CHECK(bag.Empty());
}

TEST_CASE("On subscribes and the bag is what keeps the handler alive")
{
    Event<int> event;
    std::vector<int> seen;

    {
        Subscriptions bag;
        bag.On(event, [&](int value) { seen.push_back(value); });

        event.Raise(1);
        CHECK(seen == std::vector<int>{1});
    }

    event.Raise(2);
    CHECK(seen == std::vector<int>{1});
    CHECK(event.Empty());
}

TEST_CASE("Moving a bag moves what it holds, releasing nothing")
{
    std::vector<int> released;

    {
        Subscriptions bag;
        bag.Add(Releasing(released, 1));

        Subscriptions moved = std::move(bag);
        CHECK(released.empty());
        CHECK_FALSE(moved.Empty());
    }

    CHECK(released == std::vector<int>{1});
}

TEST_CASE("Assigning over a bag releases what it held")
{
    std::vector<int> released;
    Subscriptions bag;
    bag.Add(Releasing(released, 1));

    Subscriptions replacement;
    replacement.Add(Releasing(released, 2));
    bag = std::move(replacement);

    CHECK(released == std::vector<int>{1});
}
