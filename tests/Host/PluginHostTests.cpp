#include "Host/HostCore.hpp"

#include <VoltMod/Host/Abi.hpp>
#include <algorithm>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::Borrowed;
using VoltMod::HostCore;
using VoltMod::HostEvent;
using VoltMod::HostToken;
using VoltMod::PluginContext;
using VoltMod::PluginLeaks;
using VoltMod::Text;

/** How many frame callbacks ran, the cheapest proof that a subscription is still live. */
struct CoreCounter
{
    int Calls = 0;
};

static void CountFrame(void* context)
{
    ++static_cast<CoreCounter*>(context)->Calls;
}

TEST_CASE("A context answers for the plugin it was opened for")
{
    HostCore host;
    PluginContext* plugin = host.OpenPlugin("bhop");
    REQUIRE(plugin != nullptr);

    CHECK(plugin->AbiVersion() == VoltMod::HostAbiVersion);
    CHECK(Text(plugin->Name()) == "bhop");
    CHECK(Text(plugin->HomeDirectory()) == "addons/bhop");
    CHECK(plugin->Metamod() == nullptr);
    CHECK(plugin->Detours() == nullptr);
    CHECK(host.FindPlugin("bhop") == plugin);
    CHECK(host.OpenPlugin("bhop") == nullptr);
}

TEST_CASE("GetInterface answers with each interface the context implements")
{
    HostCore host;
    PluginContext* plugin = host.OpenPlugin("bhop");

    CHECK(plugin->GetInterface(Borrowed(VoltMod::IHost::InterfaceName)) == static_cast<VoltMod::IHost*>(plugin));
    CHECK(plugin->GetInterface(Borrowed(VoltMod::IHostEvents::InterfaceName)) ==
          static_cast<VoltMod::IHostEvents*>(plugin));
    CHECK(plugin->GetInterface(Borrowed(VoltMod::IHostServices::InterfaceName)) ==
          static_cast<VoltMod::IHostServices*>(plugin));
    CHECK(plugin->GetInterface(Borrowed("VoltMod.Nothing")) == nullptr);
}

TEST_CASE("Tokens are unique across the fan-outs and the registry, and are never reused")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    CoreCounter counter;
    std::vector<HostToken> tokens{
        first->SubscribeFrame(CountFrame, &counter),
        first->SubscribeConsoleCommand(
            +[](void*, VoltMod::HostString, VoltMod::HostString, int) { return false; }, &counter),
        first->SubscribeChanged(
            +[](void*, VoltMod::HostString, bool) {}, &counter),
        second->SubscribeFrame(CountFrame, &counter),
    };

    for (const HostToken token : tokens)
        CHECK(token != 0);
    std::vector<HostToken> sorted = tokens;
    std::ranges::sort(sorted);
    CHECK(std::ranges::adjacent_find(sorted) == sorted.end());

    first->Unsubscribe(tokens.front());
    const HostToken reissued = first->SubscribeFrame(CountFrame, &counter);
    CHECK(std::ranges::find(tokens, reissued) == tokens.end());
}

TEST_CASE("Unsubscribing a token another plugin took does nothing")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    CoreCounter counter;
    const HostToken token = first->SubscribeFrame(CountFrame, &counter);

    second->Unsubscribe(token);
    host.RaiseFrame();

    CHECK(counter.Calls == 1);
}

TEST_CASE("A second plugin claiming a command name fails and the holder can be named")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    CHECK(first->ClaimCommand(Borrowed("ban")));
    CHECK_FALSE(second->ClaimCommand(Borrowed("ban")));
    CHECK(host.CommandHolder("ban") == "first");
    CHECK(host.CommandHolder("kick").empty());
    CHECK(first->ClaimCommand(Borrowed("ban")));  // its own name again is no conflict
}

TEST_CASE("Closing a context drops what the plugin still held and reports each leftover")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    CoreCounter counter;
    first->SubscribeFrame(CountFrame, &counter);
    first->SubscribeClientDisconnected(+[](void*, int) {}, &counter);
    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);
    CHECK(first->ClaimCommand(Borrowed("ban")));
    second->SubscribeFrame(CountFrame, &counter);

    host.RaiseFrame();
    CHECK(counter.Calls == 2);

    const PluginLeaks leaks = host.ClosePlugin("first");

    CHECK(leaks.Any());
    CHECK(leaks.Subscriptions == std::vector<HostEvent>{HostEvent::Frame, HostEvent::ClientDisconnected});
    CHECK(leaks.Services == std::vector<std::string>{"first.api"});
    CHECK(leaks.Commands == std::vector<std::string>{"ban"});

    counter.Calls = 0;
    host.RaiseFrame();
    CHECK(counter.Calls == 1);
    CHECK(second->Find(Borrowed("first.api")) == nullptr);
    CHECK(second->ClaimCommand(Borrowed("ban")));
    CHECK(host.FindPlugin("first") == nullptr);
}

TEST_CASE("Closing a context tells the remaining plugins its services are gone")
{
    HostCore host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    std::vector<std::string> withdrawn;
    second->SubscribeChanged(
        +[](void* context, VoltMod::HostString name, bool published) {
            if (!published)
                static_cast<std::vector<std::string>*>(context)->push_back(std::string(Text(name)));
        },
        &withdrawn);

    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);
    host.ClosePlugin("first");

    CHECK(withdrawn == std::vector<std::string>{"first.api"});
}

TEST_CASE("Closing a context that held nothing reports no leak")
{
    HostCore host;
    host.OpenPlugin("first");

    const PluginLeaks leaks = host.ClosePlugin("first");

    CHECK_FALSE(leaks.Any());
    CHECK_FALSE(host.ClosePlugin("never-loaded").Any());
}
