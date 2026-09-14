#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/** What the hook does to one outgoing join message. */
enum class AddonAction
{
    Unchanged,    ///< send the message as it is
    Send,         ///< point the client at @ref AddonDecision::Id and wait for its reconnect
    TrimToFirst,  ///< the message names several addons; keep only @ref AddonDecision::Id
    Kick,         ///< the client refused @ref AddonDecision::Id too often
};

struct AddonDecision
{
    AddonAction Action = AddonAction::Unchanged;
    uint64_t Id = 0;
    std::size_t Remaining = 0;  ///< addons still owed after Id, or cut from the message; for logging
};

/**
 * @brief Which workshop addons each client still owes, and what to send it next.
 *
 * The engine-free half of @ref Addons. Times are monotonic seconds. Keyed by SteamID, since a
 * reconnecting client's slot changes.
 */
class AddonDownloads
{
public:
    /** Require @p id of every client. Reference counted; false for id 0. */
    bool Require(uint64_t id);
    void Release(uint64_t id);

    /** Require @p id of @p steamId alone, on top of everyone's list. */
    bool RequireFor(int64_t steamId, uint64_t id);
    void ReleaseFor(int64_t steamId, uint64_t id);

    /** Whether nothing is required of anyone. */
    bool Empty() const;

    /** What every client must have, in send order. */
    std::vector<uint64_t> Required() const;

    /** What @p steamId has still to download. */
    std::vector<uint64_t> MissingFor(int64_t steamId) const;
    [[nodiscard]] bool HasMissing(int64_t steamId) const;

    /** Required addons @p steamId has downloaded or is downloading: what it mounts on connect. */
    std::vector<uint64_t> ToMount(int64_t steamId) const;

    /** The next addon for @p steamId. A repeat offer counts an attempt; past @p maxAttempts, Kick. */
    AddonDecision NextToSend(int64_t steamId, double now, int maxAttempts);

    /** Decide one outgoing join message for @p steamId. A message already sending the client away
     *  (@p reconnect) goes through, and its addon counts as sending. */
    AddonDecision DecideJoinMessage(int64_t steamId, bool reconnect, std::string_view addons, double now,
                                    int maxAttempts);

    /** Record that @p id reached @p steamId without our offer. Costs no attempt. */
    void MarkSending(int64_t steamId, uint64_t id, double now);

    /** Count the sending addon as downloaded if @p steamId reconnected within @p timeoutSec. */
    void RecordReconnect(int64_t steamId, double now, double timeoutSec);

    /** Forget every client's progress, keeping the requirements. */
    void ClearProgress();

private:
    struct Requirement
    {
        uint64_t Id = 0;
        int Holders = 0;
    };

    struct Client
    {
        std::vector<Requirement> Required;  ///< of this client alone
        std::vector<uint64_t> Downloaded;
        uint64_t Sending = 0;
        double SentAt = 0.0;
        int Attempts = 0;
    };

    static bool AddHolder(std::vector<Requirement>& list, uint64_t id);
    static void RemoveHolder(std::vector<Requirement>& list, uint64_t id);

    /** Everyone's requirements, then @p steamId's own, each once. */
    std::vector<uint64_t> RequiredFor(int64_t steamId) const;

    // A handful of entries kept in send order, so flat vectors.
    std::vector<Requirement> _everyone;
    std::unordered_map<int64_t, Client> _clients;
};

/** Split a join message's comma-separated `addons` field, skipping malformed entries. */
std::vector<uint64_t> ParseAddonList(std::string_view field);

/** Append each of @p ids that @p field lacks. Returns the ids appended. */
std::vector<uint64_t> AppendToAddonList(std::string& field, const std::vector<uint64_t>& ids);

/** Remove one entry per id in @p ids, keeping the other entries verbatim. */
void RemoveFromAddonList(std::string& field, const std::vector<uint64_t>& ids);

}  // namespace VoltMod
