#include <CS2Kit/Sdk/Detail/ClientCvarPending.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>

using CS2Kit::Sdk::ClientCvarService;
using CS2Kit::Sdk::ClientCvarStatus;
using CS2Kit::Sdk::Detail::ClientCvarPendingTable;

namespace
{

/** Callback that records the value it was handed, so tests can tell two callbacks apart. */
ClientCvarService::QueryCallback Recorder(std::string& into)
{
    return [&into](int, ClientCvarStatus, std::string_view, std::string_view value) { into = value; };
}

/** Add one query and return its cookie. */
int AddQuery(ClientCvarPendingTable& table, int slot, const std::string& name,
             ClientCvarService::QueryCallback callback, double now)
{
    const int cookie = table.NextCookie(slot);
    table.Add(slot, cookie, name, std::move(callback), now);
    return cookie;
}

}  // namespace

TEST_CASE("Take returns the query matching both cookie and name")
{
    ClientCvarPendingTable table;
    std::string seen;
    const int cookie = AddQuery(table, 3, "sensitivity", Recorder(seen), 100.0);

    auto query = table.Take(3, cookie, "sensitivity");
    REQUIRE(query.has_value());
    CHECK(query->Name == "sensitivity");
    CHECK(query->SentAtSec == doctest::Approx(100.0));

    query->Callback(3, ClientCvarStatus::ValueIntact, "sensitivity", "2.5");
    CHECK(seen == "2.5");
}

TEST_CASE("Take removes the entry it hands back")
{
    ClientCvarPendingTable table;
    std::string seen;
    const int cookie = AddQuery(table, 0, "fps_max", Recorder(seen), 0.0);

    CHECK(table.Take(0, cookie, "fps_max").has_value());
    CHECK(table.Count(0) == 0);
    CHECK_FALSE(table.Take(0, cookie, "fps_max").has_value());
}

TEST_CASE("Take rejects an answer naming a different convar")
{
    ClientCvarPendingTable table;
    std::string seen;
    const int cookie = AddQuery(table, 0, "m_yaw", Recorder(seen), 0.0);

    CHECK_FALSE(table.Take(0, cookie, "m_pitch").has_value());
    CHECK(table.Count(0) == 1);  // still outstanding, so a correct answer can still arrive
}

TEST_CASE("Take rejects an unknown cookie and an out of range slot")
{
    ClientCvarPendingTable table;
    std::string seen;
    AddQuery(table, 0, "cl_showpos", Recorder(seen), 0.0);

    CHECK_FALSE(table.Take(0, 999999, "cl_showpos").has_value());
    CHECK_FALSE(table.Take(-1, 1, "cl_showpos").has_value());
    CHECK_FALSE(table.Take(CS2Kit::Core::MaxPlayers, 1, "cl_showpos").has_value());
}

TEST_CASE("A cookie is only valid on the slot it was issued for")
{
    ClientCvarPendingTable table;
    std::string seen;
    const int cookie = AddQuery(table, 5, "sensitivity", Recorder(seen), 0.0);

    CHECK_FALSE(table.Take(6, cookie, "sensitivity").has_value());
    CHECK(table.Take(5, cookie, "sensitivity").has_value());
}

TEST_CASE("Prune drops queries at or past the timeout and keeps younger ones")
{
    ClientCvarPendingTable table;
    std::string seen;
    const int old = AddQuery(table, 1, "old", Recorder(seen), 0.0);
    const int borderline = AddQuery(table, 1, "borderline", Recorder(seen), 0.5);
    const int fresh = AddQuery(table, 1, "fresh", Recorder(seen), 5.0);

    table.Prune(1, ClientCvarPendingTable::TimeoutSec + 0.5);

    CHECK_FALSE(table.Take(1, old, "old").has_value());
    CHECK_FALSE(table.Take(1, borderline, "borderline").has_value());
    CHECK(table.Take(1, fresh, "fresh").has_value());
}

TEST_CASE("Prune only touches the slot it is given")
{
    ClientCvarPendingTable table;
    std::string seen;
    AddQuery(table, 1, "a", Recorder(seen), 0.0);
    AddQuery(table, 2, "b", Recorder(seen), 0.0);

    table.Prune(1, 1000.0);

    CHECK(table.Count(1) == 0);
    CHECK(table.Count(2) == 1);
}

TEST_CASE("Retarget replaces the callback of the query already in flight")
{
    ClientCvarPendingTable table;
    std::string first;
    std::string second;
    const int cookie = AddQuery(table, 4, "sensitivity", Recorder(first), 0.0);

    auto replacement = Recorder(second);
    CHECK(table.Retarget(4, "sensitivity", replacement));
    CHECK(table.Count(4) == 1);  // no second query was needed

    auto query = table.Take(4, cookie, "sensitivity");
    REQUIRE(query.has_value());
    query->Callback(4, ClientCvarStatus::ValueIntact, "sensitivity", "1.0");
    CHECK(first.empty());
    CHECK(second == "1.0");
}

TEST_CASE("Retarget reports false when no query for that convar is outstanding")
{
    ClientCvarPendingTable table;
    std::string seen;
    AddQuery(table, 4, "sensitivity", Recorder(seen), 0.0);

    auto callback = Recorder(seen);
    CHECK_FALSE(table.Retarget(4, "fps_max", callback));
    CHECK_FALSE(table.Retarget(9, "sensitivity", callback));
    CHECK(callback);  // left intact so the caller can still send a fresh query
}

TEST_CASE("The per slot cap refuses further queries until one is answered")
{
    ClientCvarPendingTable table;
    std::string seen;
    for (size_t i = 0; i < ClientCvarPendingTable::MaxPendingPerSlot; ++i)
        AddQuery(table, 7, "cvar" + std::to_string(i), Recorder(seen), 0.0);

    CHECK(table.Full(7));
    CHECK_FALSE(table.Full(8));
    CHECK(table.Count(7) == ClientCvarPendingTable::MaxPendingPerSlot);
}

TEST_CASE("The cap frees up once queries are answered or expire")
{
    ClientCvarPendingTable table;
    std::string seen;
    int firstCookie = -1;
    for (size_t i = 0; i < ClientCvarPendingTable::MaxPendingPerSlot; ++i)
    {
        const int cookie = AddQuery(table, 7, "cvar" + std::to_string(i), Recorder(seen), 0.0);
        if (i == 0)
            firstCookie = cookie;
    }

    CHECK(table.Take(7, firstCookie, "cvar0").has_value());
    CHECK_FALSE(table.Full(7));

    table.Prune(7, ClientCvarPendingTable::TimeoutSec);
    CHECK(table.Count(7) == 0);
}

TEST_CASE("Cookies increase and never collide with one still outstanding")
{
    ClientCvarPendingTable table;
    std::string seen;
    int previous = 0;
    for (size_t i = 0; i < ClientCvarPendingTable::MaxPendingPerSlot; ++i)
    {
        const int cookie = AddQuery(table, 2, "cvar" + std::to_string(i), Recorder(seen), 0.0);
        CHECK(cookie > previous);
        previous = cookie;
    }
    CHECK(table.Count(2) == ClientCvarPendingTable::MaxPendingPerSlot);
}

TEST_CASE("Cookies stay within the protobuf int32 range")
{
    ClientCvarPendingTable table;
    for (int i = 0; i < 32; ++i)
    {
        const int cookie = table.NextCookie(0);
        CHECK(cookie > 0);
        CHECK(static_cast<uint32_t>(cookie) <= ClientCvarPendingTable::MaxCookie);
    }
}

TEST_CASE("NextCookie refuses an out of range slot")
{
    ClientCvarPendingTable table;
    CHECK(table.NextCookie(-1) == -1);
    CHECK(table.NextCookie(CS2Kit::Core::MaxPlayers) == -1);
}

TEST_CASE("Clear drops one slot and ClearAll drops every slot")
{
    ClientCvarPendingTable table;
    std::string seen;
    AddQuery(table, 0, "a", Recorder(seen), 0.0);
    AddQuery(table, 1, "b", Recorder(seen), 0.0);

    table.Clear(0);
    CHECK(table.Count(0) == 0);
    CHECK(table.Count(1) == 1);

    table.ClearAll();
    CHECK(table.Count(1) == 0);
}

TEST_CASE("Adding to an out of range slot is a no op")
{
    ClientCvarPendingTable table;
    std::string seen;
    table.Add(-1, 1, "a", Recorder(seen), 0.0);
    table.Add(CS2Kit::Core::MaxPlayers, 1, "a", Recorder(seen), 0.0);

    CHECK(table.Count(-1) == 0);
    CHECK(table.Count(CS2Kit::Core::MaxPlayers) == 0);
}
