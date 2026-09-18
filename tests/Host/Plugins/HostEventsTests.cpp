#include "Host/Plugins/PluginHost.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::HostView;
using VoltMod::PluginHost;

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
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);

    EventsTrace trace;
    // Subscribed out of load order, to prove load order is what decides.
    second->OnFrame(+[](void* context) { Note(context, "second"); }, &trace);
    first->OnFrame(+[](void* context) { Note(context, "first-a"); }, &trace);
    first->OnFrame(+[](void* context) { Note(context, "first-b"); }, &trace);

    host.RaiseFrame();

    CHECK(trace.Calls == std::vector<std::string>{"first-a", "first-b", "second"});
}

TEST_CASE("A client connect arrives with its strings and ids intact")
{
    PluginHost host;
    HostView* plugin = host.AddPlugin("only");

    EventsTrace trace;
    plugin->OnClientConnected(
        +[](void* context, int slot, int64_t steamId, std::string_view name, std::string_view address) {
            auto& calls = static_cast<EventsTrace*>(context)->Calls;
            calls.push_back(std::string(name) + "@" + std::string(address) + "/" + std::to_string(slot) +
                            "/" + std::to_string(steamId));
        },
        &trace);

    host.RaiseClientConnected(3, 76561198000000000LL, "player", "10.0.0.2");

    CHECK(trace.Calls == std::vector<std::string>{"player@10.0.0.2/3/76561198000000000"});
}

TEST_CASE("A console command consumed by the first plugin never reaches the second")
{
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");

    EventsTrace trace;
    first->OnConsoleCommand(
        +[](void* context, std::string_view name, std::string_view, int) {
            Note(context, "first");
            return name == "ban";
        },
        &trace);
    second->OnConsoleCommand(
        +[](void* context, std::string_view, std::string_view, int) {
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
