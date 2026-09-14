#include "Workshop/AddonDownloads.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::AddonAction;
using VoltMod::AddonDownloads;
using VoltMod::AppendToAddonList;
using VoltMod::ParseAddonList;
using VoltMod::RemoveFromAddonList;

using Ids = std::vector<uint64_t>;

static constexpr int64_t kPlayer = 76561198000000001LL;
static constexpr int64_t kOtherPlayer = 76561198000000002LL;
static constexpr int kMaxAttempts = 3;
static constexpr double kTimeout = 30.0;

/** Offer the next addon at @p now and reconnect a second later. Returns the addon offered. */
static uint64_t Download(AddonDownloads& downloads, double now)
{
    const uint64_t id = downloads.NextToSend(kPlayer, now, kMaxAttempts).Id;
    downloads.RecordReconnect(kPlayer, now + 1.0, kTimeout);
    return id;
}

/** Join until nothing more is sent, each plugin's hook seeing the message in @p order. Returns the
 *  addon each reconnect was for. */
static Ids Join(const std::vector<AddonDownloads*>& order)
{
    Ids sent;
    for (double now = 0.0; sent.size() < 10; now += 1.0)
    {
        bool reconnect = false;
        std::string addons;
        for (AddonDownloads* plugin : order)
        {
            const auto decision = plugin->DecideJoinMessage(kPlayer, reconnect, addons, now, kMaxAttempts);
            if (decision.Action == AddonAction::Send || decision.Action == AddonAction::TrimToFirst)
            {
                reconnect = true;
                addons = std::to_string(decision.Id);
            }
        }
        if (!reconnect)
            break;

        sent.push_back(std::stoull(addons));
        for (AddonDownloads* plugin : order)
            plugin->RecordReconnect(kPlayer, now + 0.5, kTimeout);
    }
    return sent;
}

TEST_CASE("An addon counts as downloaded only after a prompt reconnect")
{
    AddonDownloads downloads;
    downloads.Require(100);

    const auto decision = downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);
    CHECK(decision.Action == AddonAction::Send);
    CHECK(decision.Id == 100);
    CHECK(downloads.MissingFor(kPlayer) == Ids{100});

    downloads.RecordReconnect(kPlayer, 100.0, kTimeout);
    CHECK(downloads.MissingFor(kPlayer) == Ids{100});

    Download(downloads, 200.0);
    CHECK(downloads.MissingFor(kPlayer).empty());
    CHECK(downloads.NextToSend(kPlayer, 300.0, kMaxAttempts).Action == AddonAction::Unchanged);
}

TEST_CASE("Addons go out one per reconnect, shared ones before a client's own")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);
    downloads.RequireFor(kPlayer, 300);
    downloads.RequireFor(kPlayer, 100);

    CHECK(downloads.Required() == Ids{100, 200});
    CHECK(downloads.MissingFor(kPlayer) == Ids{100, 200, 300});
    CHECK(downloads.MissingFor(kOtherPlayer) == Ids{100, 200});

    CHECK(Download(downloads, 1.0) == 100);
    CHECK(Download(downloads, 3.0) == 200);
    CHECK(downloads.HasMissing(kPlayer));
    CHECK(Download(downloads, 5.0) == 300);
    CHECK_FALSE(downloads.HasMissing(kPlayer));
    CHECK(downloads.HasMissing(kOtherPlayer));
}

TEST_CASE("Offers past the attempt cap kick the client, and a prompt reconnect resets the count")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);

    downloads.NextToSend(kPlayer, 1.0, kMaxAttempts);
    downloads.NextToSend(kPlayer, 2.0, kMaxAttempts);
    downloads.RecordReconnect(kPlayer, 3.0, kTimeout);

    for (int attempt = 1; attempt <= kMaxAttempts; ++attempt)
        CHECK(downloads.NextToSend(kPlayer, 3.0 + attempt, kMaxAttempts).Action == AddonAction::Send);

    const auto decision = downloads.NextToSend(kPlayer, 10.0, kMaxAttempts);
    CHECK(decision.Action == AddonAction::Kick);
    CHECK(decision.Id == 200);
}

TEST_CASE("Requirements are reference counted, and id zero is refused")
{
    AddonDownloads downloads;
    CHECK_FALSE(downloads.Require(0));
    CHECK_FALSE(downloads.RequireFor(kPlayer, 0));
    downloads.Release(100);

    downloads.Require(100);
    downloads.Require(100);
    downloads.RequireFor(kPlayer, 300);

    downloads.Release(100);
    CHECK(downloads.Required() == Ids{100});

    downloads.Release(100);
    downloads.Release(100);
    downloads.ReleaseFor(kPlayer, 300);
    CHECK(downloads.Empty());
}

TEST_CASE("An addon the engine is already sending costs no attempt and is not sent again")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);

    for (int attempt = 0; attempt < 10; ++attempt)
        downloads.MarkSending(kPlayer, 100, attempt);
    CHECK(downloads.NextToSend(kPlayer, 10.0, kMaxAttempts).Action == AddonAction::Send);

    downloads.RecordReconnect(kPlayer, 11.0, kTimeout);
    CHECK(downloads.NextToSend(kPlayer, 12.0, kMaxAttempts).Id == 200);
}

TEST_CASE("Clearing progress keeps the requirements")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.RequireFor(kOtherPlayer, 300);
    Download(downloads, 1.0);
    CHECK(downloads.MissingFor(kPlayer).empty());

    downloads.ClearProgress();
    CHECK(downloads.MissingFor(kPlayer) == Ids{100});
    CHECK(downloads.MissingFor(kOtherPlayer) == Ids{100, 300});
}

TEST_CASE("Plugins sharing the join message deliver each addon once, in hook order")
{
    AddonDownloads first;
    AddonDownloads second;
    first.Require(100);
    second.Require(200);
    CHECK(Join({&first, &second}) == Ids{100, 200});
    CHECK(first.MissingFor(kPlayer).empty());
    CHECK(second.MissingFor(kPlayer).empty());

    AddonDownloads third;
    AddonDownloads fourth;
    third.Require(100);
    fourth.Require(200);
    fourth.Require(100);
    CHECK(Join({&fourth, &third}) == Ids{200, 100});
    CHECK(third.MissingFor(kPlayer).empty());
}

TEST_CASE("A reconnect message keeps only its first addon, and counts it as sending")
{
    AddonDownloads downloads;
    downloads.Require(100);

    CHECK(downloads.DecideJoinMessage(kPlayer, true, "", 1.0, kMaxAttempts).Action == AddonAction::Unchanged);
    downloads.RecordReconnect(kPlayer, 2.0, kTimeout);
    CHECK(downloads.MissingFor(kPlayer) == Ids{100});

    const auto decision = downloads.DecideJoinMessage(kPlayer, true, "100,200", 3.0, kMaxAttempts);
    CHECK(decision.Action == AddonAction::TrimToFirst);
    CHECK(decision.Id == 100);
    CHECK(decision.Remaining == 1);

    downloads.RecordReconnect(kPlayer, 4.0, kTimeout);
    CHECK(downloads.MissingFor(kPlayer).empty());
}

TEST_CASE("An addons field parses as a comma separated list, skipping malformed entries")
{
    CHECK(ParseAddonList("").empty());
    CHECK(ParseAddonList("100,200,300") == Ids{100, 200, 300});
    CHECK(ParseAddonList("100,,abc,200") == Ids{100, 200});
    CHECK(ParseAddonList("100x").empty());
    CHECK(ParseAddonList("0").empty());
}

TEST_CASE("A client mounts the required addons it downloaded or is downloading")
{
    AddonDownloads downloads;
    downloads.Require(100);
    downloads.Require(200);
    CHECK(downloads.ToMount(kPlayer).empty());

    Download(downloads, 1.0);
    downloads.NextToSend(kPlayer, 3.0, kMaxAttempts);
    CHECK(downloads.ToMount(kPlayer) == Ids{100, 200});
    CHECK(downloads.ToMount(kOtherPlayer).empty());

    downloads.Release(100);
    CHECK(downloads.ToMount(kPlayer) == Ids{200});
}

TEST_CASE("Reply edits name each addon once and take back only their own, in either order")
{
    std::string single;
    CHECK(AppendToAddonList(single, {100}) == Ids{100});
    CHECK(single == "100");
    RemoveFromAddonList(single, {100, 200});
    CHECK(single.empty());

    for (const bool firstRestoresFirst : {true, false})
    {
        std::string field = "5000";
        const Ids first = AppendToAddonList(field, {100, 5000, 100});
        const Ids second = AppendToAddonList(field, {100, 200});
        CHECK(first == Ids{100});
        CHECK(second == Ids{200});
        CHECK(field == "5000,100,200");

        RemoveFromAddonList(field, firstRestoresFirst ? first : second);
        CHECK(field == (firstRestoresFirst ? "5000,200" : "5000,100"));
        RemoveFromAddonList(field, firstRestoresFirst ? second : first);
        CHECK(field == "5000");
    }
}
