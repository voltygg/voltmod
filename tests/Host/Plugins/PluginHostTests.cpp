#include "Host/Plugins/PluginHost.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using VoltMod::HostView;
using VoltMod::PluginHost;
using VoltMod::Unreleased;

/** How many frame callbacks ran, the cheapest proof that a subscription is still live. */
struct CoreCounter
{
    int Calls = 0;
};

static void CountFrame(void* context)
{
    ++static_cast<CoreCounter*>(context)->Calls;
}

TEST_CASE("A view answers for the plugin it was added for, and a name is added once")
{
    PluginHost host;
    HostView* plugin = host.AddPlugin("bhop");
    REQUIRE(plugin != nullptr);

    CHECK(plugin->Name() == "bhop");
    CHECK(host.FindPlugin("bhop") == plugin);
    CHECK(host.AddPlugin("bhop") == nullptr);
}

TEST_CASE("Tokens are unique across every event and the service table, and are never reused")
{
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");

    CoreCounter counter;
    std::vector<uint64_t> tokens{
        first->OnFrame(CountFrame, &counter),
        first->OnConsoleCommand(
            +[](void*, std::string_view, std::string_view, int) { return false; }, &counter),
        first->OnChanged(
            +[](void*, std::string_view, bool) {}, &counter),
        second->OnFrame(CountFrame, &counter),
    };

    for (const uint64_t token : tokens)
        CHECK(token != 0);
    std::vector<uint64_t> sorted = tokens;
    std::ranges::sort(sorted);
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());

    first->Unsubscribe(tokens.front());
    const uint64_t reissued = first->OnFrame(CountFrame, &counter);
    CHECK(std::ranges::find(tokens, reissued) == tokens.end());
}

TEST_CASE("Unsubscribing a token another plugin took does nothing")
{
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");

    CoreCounter counter;
    const uint64_t token = first->OnFrame(CountFrame, &counter);

    second->Unsubscribe(token);
    host.RaiseFrame();

    CHECK(counter.Calls == 1);
}

TEST_CASE("A second plugin registering a command name fails and the owner can be named")
{
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");

    CHECK(first->RegisterCommand("ban"));
    CHECK_FALSE(second->RegisterCommand("ban"));
    CHECK(host.CommandOwner("ban") == "first");
    CHECK(host.CommandOwner("kick").empty());
    CHECK(first->RegisterCommand("ban"));  // its own name again is no conflict
}

TEST_CASE("Removing a plugin drops what it still held, reports each leftover and tells its peers")
{
    PluginHost host;
    HostView* first = host.AddPlugin("first");
    HostView* second = host.AddPlugin("second");

    std::vector<std::string> withdrawn;
    second->OnChanged(
        +[](void* context, std::string_view name, bool published) {
            if (!published)
                static_cast<std::vector<std::string>*>(context)->push_back(std::string(name));
        },
        &withdrawn);

    CoreCounter counter;
    first->OnFrame(CountFrame, &counter);
    first->OnClientDisconnected(+[](void*, int) {}, &counter);
    int implementation = 7;
    first->Publish("first.api", &implementation);
    CHECK(first->RegisterCommand("ban"));
    second->OnFrame(CountFrame, &counter);

    host.RaiseFrame();
    CHECK(counter.Calls == 2);

    const Unreleased unreleased = host.RemovePlugin("first");

    CHECK(unreleased.Any());
    CHECK(unreleased.Subscriptions == std::vector<std::string_view>{"frame", "client disconnected"});
    CHECK(unreleased.Services == std::vector<std::string>{"first.api"});
    CHECK(host.CommandOwner("ban").empty());
    CHECK(withdrawn == std::vector<std::string>{"first.api"});

    counter.Calls = 0;
    host.RaiseFrame();
    CHECK(counter.Calls == 1);
    CHECK(second->Find("first.api") == nullptr);
    CHECK(second->RegisterCommand("ban"));
    CHECK(host.FindPlugin("first") == nullptr);
}

TEST_CASE("Removing a plugin that held nothing reports no leak")
{
    PluginHost host;
    host.AddPlugin("first");

    const Unreleased unreleased = host.RemovePlugin("first");

    CHECK_FALSE(unreleased.Any());
    CHECK_FALSE(host.RemovePlugin("never-loaded").Any());
}

TEST_CASE("A language one plugin sets reaches the others until the slot changes hands")
{
    PluginHost host;
    HostView* picker = host.AddPlugin("picker");
    HostView* reader = host.AddPlugin("reader");
    REQUIRE(picker != nullptr);
    REQUIRE(reader != nullptr);

    picker->SetPlayerLanguage(2, "ru");
    CHECK(reader->PlayerLanguage(2) == "ru");

    host.RaiseClientDisconnected(2);
    CHECK(reader->PlayerLanguage(2).empty());

    picker->SetPlayerLanguage(2, "ru");
    host.RaiseClientConnected(2, 76561198000000000LL, "next", "10.0.0.3");
    CHECK(reader->PlayerLanguage(2).empty());
}
