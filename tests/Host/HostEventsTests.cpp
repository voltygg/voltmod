#include "Host/HostCore.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::HostCore;
using VoltMod::HostString;
using VoltMod::HostToken;
using VoltMod::PluginContext;
using VoltMod::Text;

/** The callback context every case here passes: one list of who ran, in order. */
struct EventsTrace
{
    std::vector<std::string> Calls;
};

static void Note(void* context, const char* who)
{
    static_cast<EventsTrace*>(context)->Calls.push_back(who);
}

TEST_CASE("Frame callbacks run in plugin load order, then subscription order")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    EventsTrace trace;
    // Subscribed out of load order, to prove load order is what decides.
    second->SubscribeFrame(+[](void* context) { Note(context, "second"); }, &trace);
    first->SubscribeFrame(+[](void* context) { Note(context, "first-a"); }, &trace);
    first->SubscribeFrame(+[](void* context) { Note(context, "first-b"); }, &trace);

    host.RaiseFrame();

    CHECK(trace.Calls == std::vector<std::string>{"first-a", "first-b", "second"});
}

TEST_CASE("Every plugin sees a client connect, in load order")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    EventsTrace trace;
    const auto record = +[](void* context, int slot, int64_t steamId, HostString name, HostString address) {
        auto& calls = static_cast<EventsTrace*>(context)->Calls;
        calls.push_back(std::string(Text(name)) + "@" + std::string(Text(address)) + "/" + std::to_string(slot) + "/" +
                        std::to_string(steamId));
    };
    first->SubscribeClientConnected(record, &trace);
    second->SubscribeClientConnected(record, &trace);

    host.RaiseClientConnected(3, 76561198000000000LL, "player", "10.0.0.2");

    CHECK(trace.Calls.size() == 2);
    CHECK(trace.Calls.front() == "player@10.0.0.2/3/76561198000000000");
    CHECK(trace.Calls.back() == trace.Calls.front());
}

TEST_CASE("A console command consumed by the first plugin never reaches the second")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    EventsTrace trace;
    first->SubscribeConsoleCommand(
        +[](void* context, HostString name, HostString, int) {
            Note(context, "first");
            return Text(name) == "ban";
        },
        &trace);
    second->SubscribeConsoleCommand(
        +[](void* context, HostString, HostString, int) {
            Note(context, "second");
            return false;
        },
        &trace);

    CHECK(host.RaiseConsoleCommand("ban", "player 5", 3));
    CHECK(trace.Calls == std::vector<std::string>{"first"});

    trace.Calls.clear();
    CHECK_FALSE(host.RaiseConsoleCommand("kick", "player", 3));
    CHECK(trace.Calls == std::vector<std::string>{"first", "second"});
}

/** Holds the token one callback removes while the pass that reaches it is still running. */
struct EventsRemoval
{
    PluginContext* Plugin = nullptr;
    HostToken Target = 0;
    std::vector<std::string> Calls;
};

TEST_CASE("A callback unsubscribed earlier in the same pass does not run")
{
    HostCore host;
    PluginContext* plugin = host.OpenPlugin("only");

    EventsRemoval state;
    state.Plugin = plugin;
    plugin->SubscribeFrame(
        +[](void* context) {
            auto& removal = *static_cast<EventsRemoval*>(context);
            removal.Calls.push_back("first");
            removal.Plugin->Unsubscribe(removal.Target);
        },
        &state);
    state.Target = plugin->SubscribeFrame(
        +[](void* context) { static_cast<EventsRemoval*>(context)->Calls.push_back("second"); }, &state);
    plugin->SubscribeFrame(
        +[](void* context) { static_cast<EventsRemoval*>(context)->Calls.push_back("third"); }, &state);

    host.RaiseFrame();
    CHECK(state.Calls == std::vector<std::string>{"first", "third"});

    state.Calls.clear();
    host.RaiseFrame();
    CHECK(state.Calls == std::vector<std::string>{"first", "third"});
}

/** Lets one callback subscribe another exactly once, mid-pass. */
struct EventsAddition
{
    PluginContext* Plugin = nullptr;
    int Passes = 0;
    std::vector<std::string> Calls;
};

TEST_CASE("A callback subscribed during a pass first runs in the next one")
{
    HostCore host;
    PluginContext* plugin = host.OpenPlugin("only");

    EventsAddition state;
    state.Plugin = plugin;
    plugin->SubscribeFrame(
        +[](void* context) {
            auto& addition = *static_cast<EventsAddition*>(context);
            addition.Calls.push_back("first");
            if (addition.Passes++ > 0)
                return;
            addition.Plugin->SubscribeFrame(
                +[](void* inner) { static_cast<EventsAddition*>(inner)->Calls.push_back("late"); }, context);
        },
        &state);

    host.RaiseFrame();
    CHECK(state.Calls == std::vector<std::string>{"first"});

    state.Calls.clear();
    host.RaiseFrame();
    CHECK(state.Calls == std::vector<std::string>{"first", "late"});
}

/** What the check-transmit callback was handed, to compare against what was raised. */
struct EventsTransmit
{
    CCheckTransmitInfo** List = nullptr;
    int Count = -1;
};

TEST_CASE("Check transmit hands the engine list through untouched")
{
    HostCore host;
    PluginContext* plugin = host.OpenPlugin("only");

    EventsTransmit seen;
    plugin->SubscribeCheckTransmit(
        +[](void* context, CCheckTransmitInfo** list, int count) {
            auto& transmit = *static_cast<EventsTransmit*>(context);
            transmit.List = list;
            transmit.Count = count;
        },
        &seen);

    CCheckTransmitInfo* entries[2] = {};
    host.RaiseCheckTransmit(entries, 2);

    CHECK(seen.List == entries);
    CHECK(seen.Count == 2);
}
