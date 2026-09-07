#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SubscriptionScope.hpp>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using VoltMod::Event;
using VoltMod::Subscription;
using VoltMod::SubscriptionScope;

/** A Subscription whose release appends @p id to @p released. */
static Subscription Releasing(std::vector<int>& released, int id)
{
    return Subscription([&released, id] { released.push_back(id); });
}

TEST_CASE("A scope releases what it holds when it is destroyed, newest first")
{
    std::vector<int> released;

    {
        SubscriptionScope scope;
        scope.Add(Releasing(released, 1));
        scope.Add(Releasing(released, 2));
        scope.Add(Releasing(released, 3));

        CHECK_FALSE(scope.Empty());
        CHECK(released.empty());
    }

    CHECK(released == std::vector<int>{3, 2, 1});
}

TEST_CASE("Clear releases everything and leaves the scope empty")
{
    std::vector<int> released;
    SubscriptionScope scope;

    scope.Add(Releasing(released, 1));
    scope.Add(Releasing(released, 2));
    scope.Clear();

    CHECK(released == std::vector<int>{2, 1});
    CHECK(scope.Empty());

    scope.Clear();
    CHECK(released.size() == 2);
}

TEST_CASE("A fresh scope is empty and clearing it does nothing")
{
    SubscriptionScope scope;

    CHECK(scope.Empty());
    scope.Clear();
    CHECK(scope.Empty());
}

TEST_CASE("The scope is what keeps a subscribed handler alive")
{
    Event<int> event;
    std::vector<int> seen;

    {
        SubscriptionScope scope;
        scope.Add(event += [&](int value) { seen.push_back(value); });

        event.Raise(1);
        CHECK(seen == std::vector<int>{1});
    }

    event.Raise(2);
    CHECK(seen == std::vector<int>{1});
    CHECK(event.Empty());
}

TEST_CASE("Moving a scope moves what it holds, releasing nothing")
{
    std::vector<int> released;

    {
        SubscriptionScope scope;
        scope.Add(Releasing(released, 1));

        SubscriptionScope moved = std::move(scope);
        CHECK(released.empty());
        CHECK_FALSE(moved.Empty());
    }

    CHECK(released == std::vector<int>{1});
}

TEST_CASE("Assigning over a scope releases what it held")
{
    std::vector<int> released;
    SubscriptionScope scope;
    scope.Add(Releasing(released, 1));

    SubscriptionScope replacement;
    replacement.Add(Releasing(released, 2));
    scope = std::move(replacement);

    CHECK(released == std::vector<int>{1});
}
