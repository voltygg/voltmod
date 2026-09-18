#include "Host/PluginHost.hpp"

#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::Borrowed;
using VoltMod::HostString;
using VoltMod::PluginContext;
using VoltMod::PluginHost;
using VoltMod::Text;

/** The Changed callback context: each entry is the name with a leading + or -. */
struct ServicesLog
{
    std::vector<std::string> Entries;
};

static void RecordChange(void* context, HostString name, bool published)
{
    auto& log = *static_cast<ServicesLog*>(context);
    log.Entries.push_back((published ? "+" : "-") + std::string(Text(name)));
}

TEST_CASE("Find returns what a plugin published, and nothing once it withdraws")
{
    PluginHost host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);

    CHECK(second->Find(Borrowed("first.api")) == &implementation);
    CHECK(host.ServiceOwner("first.api") == "first");
    CHECK(second->Find(Borrowed("missing.api")) == nullptr);

    first->Unpublish(Borrowed("first.api"));

    CHECK(second->Find(Borrowed("first.api")) == nullptr);
    CHECK(host.ServiceOwner("first.api").empty());
}

TEST_CASE("Unpublishing a name another plugin owns leaves that entry alone")
{
    PluginHost host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);

    second->Unpublish(Borrowed("first.api"));

    CHECK(first->Find(Borrowed("first.api")) == &implementation);
    CHECK(host.ServiceOwner("first.api") == "first");
}

TEST_CASE("Changed reports a publish and a withdrawal to every subscriber")
{
    PluginHost host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    ServicesLog log;
    second->OnChanged(RecordChange, &log);

    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);
    first->Unpublish(Borrowed("first.api"));

    CHECK(log.Entries == std::vector<std::string>{"+first.api", "-first.api"});
}

TEST_CASE("A late Changed subscriber is replayed what is already in the table")
{
    PluginHost host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    int one = 1;
    int two = 2;
    first->Publish(Borrowed("first.one"), &one);
    first->Publish(Borrowed("first.two"), &two);

    ServicesLog log;
    second->OnChanged(RecordChange, &log);

    CHECK(log.Entries == std::vector<std::string>{"+first.one", "+first.two"});
}

TEST_CASE("Unsubscribing from Changed stops the notifications")
{
    PluginHost host;
    PluginContext* first = host.OpenPlugin("first");
    PluginContext* second = host.OpenPlugin("second");

    ServicesLog log;
    second->Unsubscribe(second->OnChanged(RecordChange, &log));

    int implementation = 7;
    first->Publish(Borrowed("first.api"), &implementation);

    CHECK(log.Entries.empty());
}
