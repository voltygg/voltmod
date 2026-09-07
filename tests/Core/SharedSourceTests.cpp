#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SharedSource.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Event;
using VoltMod::SharedSource;

// What a service sharing one engine-side source across several events needs: start once when the
// first of them begins listening, stop once when the last one goes quiet.

TEST_CASE("The first handler starts the source and the last one to drop stops it")
{
    int starts = 0;
    int stops = 0;
    SharedSource source(
        "Test",
        [&] {
            ++starts;
            return true;
        },
        [&] { ++stops; });
    Event<int> event(source.Lifecycle());

    {
        auto first = event += [](int) {};
        auto second = event += [](int) {};
        CHECK(starts == 1);
        CHECK(stops == 0);
        // An Event reports only its empty-to-first and last-to-empty transitions, so two handlers
        // on one event are one subscriber here.
        CHECK(source.Subscribers() == 1);
    }

    CHECK(stops == 1);
    CHECK(source.Subscribers() == 0);
}

TEST_CASE("One source is shared across events with different handler signatures")
{
    int starts = 0;
    int stops = 0;
    SharedSource source(
        "Test",
        [&] {
            ++starts;
            return true;
        },
        [&] { ++stops; });

    Event<int> pre(source.Lifecycle());
    Event<int, const std::string&> withPayload(source.Lifecycle());

    auto a = pre += [](int) {};
    auto b = withPayload += [](int, const std::string&) {};
    CHECK(starts == 1);
    CHECK(source.Subscribers() == 2);

    b.Reset();
    CHECK(stops == 0);  // pre is still listening

    a.Reset();
    CHECK(stops == 1);
}

TEST_CASE("A refused start yields an empty subscription and never stops")
{
    int stops = 0;
    SharedSource source("Test", [] { return false; }, [&] { ++stops; });
    Event<int> event(source.Lifecycle());

    {
        auto refused = event += [](int) {};
        CHECK_FALSE(static_cast<bool>(refused));
        CHECK(event.Empty());
        CHECK(source.Subscribers() == 0);
    }

    CHECK(stops == 0);
}

TEST_CASE("A refused start is retried by the next subscriber")
{
    bool ready = false;
    int starts = 0;
    SharedSource source(
        "Test",
        [&] {
            if (!ready)
                return false;
            ++starts;
            return true;
        },
        [] {});
    Event<int> event(source.Lifecycle());

    auto refused = event += [](int) {};
    CHECK_FALSE(static_cast<bool>(refused));
    CHECK(starts == 0);

    ready = true;
    auto accepted = event += [](int) {};
    CHECK(static_cast<bool>(accepted));
    CHECK(starts == 1);
}
