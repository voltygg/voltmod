#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SharedLifecycle.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Event;
using VoltMod::SharedLifecycle;

// What a service sharing one engine-side source across several events needs: start once when the
// first of them begins listening, stop once when the last one goes quiet.

TEST_CASE("The first handler starts the source and the last one to drop stops it")
{
    int starts = 0;
    int stops = 0;
    SharedLifecycle lifecycle(
        "Test",
        [&] {
            ++starts;
            return true;
        },
        [&] { ++stops; });
    Event<int> event(lifecycle.ForEvent());

    {
        auto first = event += [](int) {};
        auto second = event += [](int) {};
        CHECK(starts == 1);
        CHECK(stops == 0);
        // An Event reports only its empty-to-first and last-to-empty transitions, so two handlers
        // on one event are one subscriber here.
        CHECK(lifecycle.ListeningEvents() == 1);
    }

    CHECK(stops == 1);
    CHECK(lifecycle.ListeningEvents() == 0);
}

TEST_CASE("One source is shared across events with different handler signatures")
{
    int starts = 0;
    int stops = 0;
    SharedLifecycle lifecycle(
        "Test",
        [&] {
            ++starts;
            return true;
        },
        [&] { ++stops; });

    Event<int> pre(lifecycle.ForEvent());
    Event<int, const std::string&> withPayload(lifecycle.ForEvent());

    auto a = pre += [](int) {};
    auto b = withPayload += [](int, const std::string&) {};
    CHECK(starts == 1);
    CHECK(lifecycle.ListeningEvents() == 2);

    b.Reset();
    CHECK(stops == 0);  // pre is still listening

    a.Reset();
    CHECK(stops == 1);
}

// Event's own Lifecycle refusal is covered in EventTests; what matters here is that a refusal
// leaves the shared source with no listeners and is retried by whoever subscribes next.
TEST_CASE("A refused start counts no listener and is retried by the next subscriber")
{
    bool ready = false;
    int starts = 0;
    int stops = 0;
    SharedLifecycle lifecycle(
        "Test",
        [&] {
            if (!ready)
                return false;
            ++starts;
            return true;
        },
        [&] { ++stops; });
    Event<int> event(lifecycle.ForEvent());

    {
        auto refused = event += [](int) {};
        CHECK_FALSE(static_cast<bool>(refused));
        CHECK(starts == 0);
        CHECK(lifecycle.ListeningEvents() == 0);
    }
    CHECK(stops == 0);

    ready = true;
    auto accepted = event += [](int) {};
    CHECK(static_cast<bool>(accepted));
    CHECK(starts == 1);
    CHECK(lifecycle.ListeningEvents() == 1);
}
