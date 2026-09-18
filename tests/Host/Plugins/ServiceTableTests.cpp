#include "Host/Plugins/ServiceTable.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltMod::ServiceTable;

/** Stands in for a plugin: the table only ever compares the pointer. */
static const int First = 1;
static const int Second = 2;

/** The Changed callback context: each entry is the name with a leading + or -. */
struct ServicesLog
{
    std::vector<std::string> Entries;
};

static void RecordChange(void* context, std::string_view name, bool published)
{
    auto& log = *static_cast<ServicesLog*>(context);
    log.Entries.push_back((published ? "+" : "-") + std::string(name));
}

static uint64_t Watch(ServiceTable& table, ServicesLog& log)
{
    table.Changed().Add(1, 1, RecordChange, &log);
    table.NotifyPublished(RecordChange, &log);
    return 1;
}

TEST_CASE("Find returns what an owner published, and nothing once it withdraws")
{
    ServiceTable table;
    int implementation = 7;
    table.Publish(&First, "first.api", &implementation);

    CHECK(table.Find("first.api") == &implementation);
    CHECK(table.Find("missing.api") == nullptr);

    table.Unpublish(&First, "first.api");

    CHECK(table.Find("first.api") == nullptr);
}

TEST_CASE("Unpublishing a name another owner holds leaves that entry alone")
{
    ServiceTable table;
    int implementation = 7;
    table.Publish(&First, "first.api", &implementation);

    table.Unpublish(&Second, "first.api");

    CHECK(table.Find("first.api") == &implementation);
}

TEST_CASE("A peer cannot swap out a live pointer, and the owner can refresh it")
{
    ServiceTable table;
    int mine = 7;
    int theirs = 9;
    table.Publish(&First, "first.api", &mine);

    table.Publish(&Second, "first.api", &theirs);
    CHECK(table.Find("first.api") == &mine);

    table.Publish(&First, "first.api", &theirs);
    CHECK(table.Find("first.api") == &theirs);
}

TEST_CASE("Changed reports a publish and a withdrawal to every subscriber")
{
    ServiceTable table;
    ServicesLog log;
    Watch(table, log);

    int implementation = 7;
    table.Publish(&First, "first.api", &implementation);
    table.Unpublish(&First, "first.api");

    CHECK(log.Entries == std::vector<std::string>{"+first.api", "-first.api"});
}

TEST_CASE("A late subscriber is replayed what is already in the table")
{
    ServiceTable table;
    int one = 1;
    int two = 2;
    table.Publish(&First, "first.one", &one);
    table.Publish(&First, "first.two", &two);

    ServicesLog log;
    Watch(table, log);

    CHECK(log.Entries == std::vector<std::string>{"+first.one", "+first.two"});
}

TEST_CASE("Unsubscribing from Changed stops the notifications")
{
    ServiceTable table;
    ServicesLog log;
    table.Changed().Remove(Watch(table, log));

    int implementation = 7;
    table.Publish(&First, "first.api", &implementation);

    CHECK(log.Entries.empty());
}

TEST_CASE("Releasing an owner withdraws everything it published and names each one")
{
    ServiceTable table;
    int one = 1;
    int two = 2;
    table.Publish(&First, "first.one", &one);
    table.Publish(&Second, "second.api", &two);
    table.Publish(&First, "first.two", &two);

    ServicesLog log;
    Watch(table, log);
    log.Entries.clear();

    CHECK(table.RemoveAll(&First) == std::vector<std::string>{"first.one", "first.two"});
    CHECK(log.Entries == std::vector<std::string>{"-first.one", "-first.two"});
    CHECK(table.Find("second.api") == &two);
}
