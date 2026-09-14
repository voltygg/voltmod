#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/** What the hook does to one outgoing join message. */
enum class AddonAction
{
    Leave,       ///< send the message unchanged
    Send,        ///< point the client at @ref AddonDecision::Id and wait for its reconnect
    KeepFirst,   ///< the message names several addons; keep only @ref AddonDecision::Id
    DropClient,  ///< the client refused @ref AddonDecision::Id too often
};

struct AddonDecision
{
    AddonAction Action = AddonAction::Leave;
    uint64_t Id = 0;
    std::size_t Remaining = 0;  ///< addons still owed after Id, or cut from the message; for logging
};

/**
 * @brief Which workshop addons each client still owes, and what to send it next.
 *
 * The engine-free half of @ref Addons, so it can be unit tested. Times are the caller's monotonic
 * seconds. Progress is keyed by SteamID because a client's slot changes as it reconnects.
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

    /** Pick the next addon for @p steamId. Offering the same one again counts an attempt; past
     *  @p maxAttempts the answer is DropClient. */
    AddonDecision NextToSend(int64_t steamId, double now, int maxAttempts);

    /**
     * Decide what to do with one outgoing join message for @p steamId.
     *
     * @p reconnect is whether the message already sends the client away to load @p addons. Such a
     * message, from the engine or another plugin's hook earlier in the chain, goes through and its
     * addon counts as sending, so plugins share the message instead of overwriting each other.
     */
    AddonDecision DecideJoinMessage(int64_t steamId, bool reconnect, std::string_view addons, double now,
                                    int maxAttempts);

    /** Record that @p id is on its way to @p steamId without our offer. Costs no attempt. */
    void MarkSending(int64_t steamId, uint64_t id, double now);

    /** Count what was sending as downloaded if @p steamId came back within @p timeoutSec. The server
     *  gets no other sign that a download finished. */
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

    // A handful of entries kept in send order, so flat vectors.
    std::vector<Requirement> _everyone;
    std::unordered_map<int64_t, Client> _clients;
};

/** Split a join message's comma-separated `addons` field, skipping malformed entries. */
std::vector<uint64_t> ParseAddonList(std::string_view field);

}  // namespace VoltMod
