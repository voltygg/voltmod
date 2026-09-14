#include "Workshop/AddonDownloads.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::AddonAction;
using VoltMod::AddonDownloads;
using VoltMod::ParseAddonList;

static constexpr int64_t kPlayer = 76561198000000001LL;
static constexpr int64_t kOtherPlayer = 76561198000000002LL;
static constexpr int kMaxAttempts = 3;

/** One outgoing join message, as each plugin's hook sees it in turn. */
struct JoinMessage
{
    bool Reconnect = false;
    std::string Addons;
};

/** Apply one plugin's decision to @p message the way its hook does. */
static void RunHook(AddonDownloads& plugin, JoinMessage& message, double now)
{
    const auto decision = plugin.DecideJoinMessage(kPlayer, message.Reconnect, message.Addons, now, kMaxAttempts);
    if (decision.Action == AddonAction::Send)
        message.Reconnect = true;
    if (decision.Action == AddonAction::Send || decision.Action == AddonAction::KeepFirst)
        message.Addons = std::to_string(decision.Id);
}

/** Join until nothing more is sent, running every plugin's hook in @p order and reconnecting
 *  promptly. Returns the addon each reconnect was for. */
static std::vector<uint64_t> Join(const std::vector<AddonDownloads*>& order)
{
    std::vector<uint64_t> sent;
    double now = 0.0;
    for (int cycle = 0; cycle < 10; ++cycle)
    {
        JoinMessage message;
        for (AddonDownloads* plugin : order)
            RunHook(*plugin, message, now);
        if (!message.Reconnect)
            break;

        sent.push_back(std::stoull(message.Addons));
        now += 1.0;
        for (AddonDownloads* plugin : order)
            plugin->RecordReconnect(kPlayer, now, 30.0);
    }
    return sent;
}

TEST_CASE("A required addon is missing until the client reconnects")
{
    AddonDownloads downloads;
    downloads.Require(100);

    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});

    const auto decision = downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);
    CHECK(decision.Action == AddonAction::Send);
    CHECK(decision.Id == 100);

    // Sending is not evidence the client took it.
    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});

    downloads.RecordReconnect(kPlayer, 20.0, 30.0);
    CHECK(downloads.MissingFor(kPlayer).empty());
}

TEST_CASE("HasMissing agrees with MissingFor")
{
    AddonDownloads downloads;
    CHECK_FALSE(downloads.HasMissing(kPlayer));

    downloads.Require(100);
    downloads.RequireFor(kPlayer, 200);
    CHECK(downloads.HasMissing(kPlayer));
    CHECK(downloads.HasMissing(kOtherPlayer));

    downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);
    downloads.RecordReconnect(kPlayer, 20.0, 30.0);
    CHECK(downloads.HasMissing(kPlayer));  // its own addon is still owed

    downloads.NextToSend(kPlayer, 30.0, kMaxAttempts);
    downloads.RecordReconnect(kPlayer, 40.0, 30.0);
    CHECK(downloads.MissingFor(kPlayer).empty());
    CHECK_FALSE(downloads.HasMissing(kPlayer));
}

TEST_CASE("A reconnect after the timeout does not count the addon as downloaded")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);

    downloads.RecordReconnect(kPlayer, 100.0, 30.0);
    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});
}

TEST_CASE("Addons are sent one at a time in the order they were required")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);

    CHECK(downloads.NextToSend(kPlayer, 1.0, kMaxAttempts).Id == 100);
    downloads.RecordReconnect(kPlayer, 2.0, 30.0);

    CHECK(downloads.NextToSend(kPlayer, 3.0, kMaxAttempts).Id == 200);
    downloads.RecordReconnect(kPlayer, 4.0, 30.0);

    CHECK(downloads.NextToSend(kPlayer, 5.0, kMaxAttempts).Action == AddonAction::Leave);
}

TEST_CASE("Offering the same addon past the attempt cap drops the client")
{
    AddonDownloads downloads;
    downloads.Require(100);

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        CHECK(downloads.NextToSend(kPlayer, attempt, kMaxAttempts).Action == AddonAction::Send);

    const auto decision = downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);
    CHECK(decision.Action == AddonAction::DropClient);
    CHECK(decision.Id == 100);
}

TEST_CASE("A prompt reconnect resets the attempt count")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);

    downloads.NextToSend(kPlayer, 1.0, kMaxAttempts);
    downloads.NextToSend(kPlayer, 2.0, kMaxAttempts);  // second offer of 100
    downloads.RecordReconnect(kPlayer, 3.0, 30.0);

    // 200 starts its own count.
    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        CHECK(downloads.NextToSend(kPlayer, 3.0 + attempt, kMaxAttempts).Action == AddonAction::Send);
}

TEST_CASE("Requirements are reference counted")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(100);

    downloads.Release(100);
    CHECK(downloads.Required() == std::vector<uint64_t>{100});
    CHECK_FALSE(downloads.Empty());

    downloads.Release(100);
    CHECK(downloads.Required().empty());
    CHECK(downloads.Empty());
}

TEST_CASE("Releasing an addon nobody required does nothing")
{
    AddonDownloads downloads;
    downloads.Release(100);
    downloads.Require(100);
    downloads.Release(100);
    downloads.Release(100);

    CHECK(downloads.Empty());
}

TEST_CASE("Addon id zero is refused")
{
    AddonDownloads downloads;
    CHECK_FALSE(downloads.Require(0));
    CHECK_FALSE(downloads.RequireFor(kPlayer, 0));
    CHECK(downloads.Empty());
}

TEST_CASE("A per-client requirement reaches only that client")
{
    AddonDownloads downloads;
    downloads.RequireFor(kPlayer, 300);

    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{300});
    CHECK(downloads.MissingFor(kOtherPlayer).empty());
    CHECK(downloads.Required().empty());
    CHECK_FALSE(downloads.Empty());

    downloads.ReleaseFor(kPlayer, 300);
    CHECK(downloads.Empty());
}

TEST_CASE("A per-client requirement comes after the shared list")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.RequireFor(kPlayer, 300);

    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100, 300});
}

TEST_CASE("An addon required of everyone and of one client is listed once")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.RequireFor(kPlayer, 100);

    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});
}

TEST_CASE("An addon the engine is already sending is not sent again")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);

    downloads.MarkSending(kPlayer, 100, 5.0);
    downloads.RecordReconnect(kPlayer, 6.0, 30.0);

    CHECK(downloads.NextToSend(kPlayer, 7.0, kMaxAttempts).Id == 200);
}

TEST_CASE("An addon the engine is sending costs no attempt")
{
    AddonDownloads downloads;
    downloads.Require(100);

    for (int attempt = 0; attempt < 10; ++attempt)
        downloads.MarkSending(kPlayer, 100, attempt);

    CHECK(downloads.NextToSend(kPlayer, 20.0, kMaxAttempts).Action == AddonAction::Send);
}

TEST_CASE("Clearing progress keeps the requirements")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.RequireFor(kOtherPlayer, 300);
    downloads.NextToSend(kPlayer, 1.0, kMaxAttempts);
    downloads.RecordReconnect(kPlayer, 2.0, 30.0);
    CHECK(downloads.MissingFor(kPlayer).empty());

    downloads.ClearProgress();

    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});
    CHECK(downloads.MissingFor(kOtherPlayer) == std::vector<uint64_t>{100, 300});
}

TEST_CASE("Two plugins requiring different addons deliver both, one per reconnect")
{
    AddonDownloads first;
    AddonDownloads second;
    first.Require(100);
    second.Require(200);

    CHECK(Join({&first, &second}) == std::vector<uint64_t>{100, 200});
    CHECK(first.MissingFor(kPlayer).empty());
    CHECK(second.MissingFor(kPlayer).empty());
}

TEST_CASE("Hook order between plugins decides only which addon goes first")
{
    AddonDownloads first;
    AddonDownloads second;
    first.Require(100);
    second.Require(200);

    CHECK(Join({&second, &first}) == std::vector<uint64_t>{200, 100});
    CHECK(first.MissingFor(kPlayer).empty());
    CHECK(second.MissingFor(kPlayer).empty());
}

TEST_CASE("An addon two plugins both require is sent once")
{
    AddonDownloads first;
    AddonDownloads second;
    first.Require(100);
    second.Require(100);

    CHECK(Join({&first, &second}) == std::vector<uint64_t>{100});
}

TEST_CASE("A reconnect message naming several addons keeps the first")
{
    AddonDownloads downloads;
    const auto decision = downloads.DecideJoinMessage(kPlayer, true, "100,200", 1.0, kMaxAttempts);

    CHECK(decision.Action == AddonAction::KeepFirst);
    CHECK(decision.Id == 100);
    CHECK(decision.Remaining == 1);
}

TEST_CASE("A reconnect message naming no addon changes nothing")
{
    AddonDownloads downloads;
    downloads.Require(100);

    CHECK(downloads.DecideJoinMessage(kPlayer, true, "", 1.0, kMaxAttempts).Action == AddonAction::Leave);
    downloads.RecordReconnect(kPlayer, 2.0, 30.0);
    CHECK(downloads.MissingFor(kPlayer) == std::vector<uint64_t>{100});
}

TEST_CASE("An addons field parses as a comma separated list")
{
    CHECK(ParseAddonList("") == std::vector<uint64_t>{});
    CHECK(ParseAddonList("100") == std::vector<uint64_t>{100});
    CHECK(ParseAddonList("100,200,300") == std::vector<uint64_t>{100, 200, 300});
}

TEST_CASE("A malformed addons entry is skipped rather than misread")
{
    CHECK(ParseAddonList("100,,200") == std::vector<uint64_t>{100, 200});
    CHECK(ParseAddonList("100,abc,200") == std::vector<uint64_t>{100, 200});
    CHECK(ParseAddonList("100x") == std::vector<uint64_t>{});
    CHECK(ParseAddonList("0") == std::vector<uint64_t>{});
}
