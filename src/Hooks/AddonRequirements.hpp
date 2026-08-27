#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/** What to do about one client's outstanding addons right now. */
enum class AddonStep
{
    Nothing,  ///< it has everything it owes
    Send,     ///< send @ref AddonDecision::Id and wait for the reconnect
    GiveUp,   ///< the same addon has been refused too many times; drop the client
};

/** @ref AddonRequirements::NextFor's answer. */
struct AddonDecision
{
    AddonStep Step = AddonStep::Nothing;
    uint64_t Id = 0;  ///< the addon to send, when @ref Step is Send
};

/**
 * @brief Which workshop addons each client still owes, and what to send it next.
 *
 * Split out of @ref Addons so the merge, credit and retry rules are testable without the engine.
 * Time is caller-supplied (seconds, any monotonic origin), as in @ref ClientCvarPendingTable.
 *
 * Requirements are reference counted: two features may need the same addon, and an id stops being
 * required only when the last holder releases it. Progress is keyed by SteamID, not slot, because
 * a client cycling through downloads reconnects and its slot changes.
 */
class AddonRequirements
{
public:
    /** Require @p id of every client. False when @p id is 0. Idempotent per holder: each call
     *  takes one reference. */
    bool Require(uint64_t id);

    /** Release one reference taken by @ref Require. */
    void Release(uint64_t id);

    /** Require @p id of @p steamId alone, on top of the global list. */
    bool RequireFor(int64_t steamId, uint64_t id);

    /** Release one reference taken by @ref RequireFor. */
    void ReleaseFor(int64_t steamId, uint64_t id);

    /** Whether nothing is required of anyone. */
    bool Empty() const;

    /** What every client is required to have, in the order they are sent. */
    std::vector<uint64_t> Required() const;

    /** What @p steamId still has to fetch: the global list plus its own, minus what it has. */
    std::vector<uint64_t> MissingFor(int64_t steamId) const;

    /**
     * Decide what to send @p steamId now, counting this as one more attempt at the same addon.
     *
     * Offering the same id again means the last offer was not taken, so the attempt count rises;
     * past @p maxAttempts the answer is @ref AddonStep::GiveUp rather than an endless reconnect.
     */
    AddonDecision NextFor(int64_t steamId, double now, int maxAttempts);

    /**
     * Record that @p id is already on its way to @p steamId without this deciding to send it.
     *
     * The engine's own changelevel signon carries an addon; crediting it on the reconnect stops
     * that addon being sent a second time. It is not one of our offers, so it costs no attempt.
     */
    void NoteInFlight(int64_t steamId, uint64_t id, double now);

    /**
     * Credit @p steamId with whatever was in flight if it reconnected within @p timeoutSec.
     *
     * Nothing tells the server a download finished; coming back promptly is the evidence. A client
     * returning later than that was doing something else and starts that addon over.
     */
    void CreditReconnect(int64_t steamId, double now, double timeoutSec);

    /** Forget every client's progress, keeping the requirements themselves. */
    void ForgetClients();

private:
    /** One required id and how many holders want it. */
    struct Requirement
    {
        uint64_t Id = 0;
        int Refs = 0;
    };

    /** One client's progress through its list. */
    struct ClientState
    {
        std::vector<Requirement> Extra;     ///< required of this SteamID alone
        std::vector<uint64_t> Downloaded;   ///< confirmed by a reconnect
        uint64_t Sending = 0;               ///< sent but not yet confirmed
        double SentAt = 0.0;                ///< when Sending was set, for the timeout
        int Attempts = 0;                   ///< offers of Sending so far
    };

    static bool Take(std::vector<Requirement>& list, uint64_t id);
    static void Drop(std::vector<Requirement>& list, uint64_t id);

    // Insertion order is send order, and the lists are a handful of entries, so a flat vector
    // beats a keyed container on every operation here.
    std::vector<Requirement> _global;
    std::unordered_map<int64_t, ClientState> _clients;
};

/**
 * Split a signon message's `addons` field into ids.
 *
 * The field is comma-separated and the engine may put several in it on a changelevel, which a
 * client cannot act on - it handles one addon per connection cycle. Malformed entries are skipped.
 */
std::vector<uint64_t> ParseAddonList(std::string_view field);

}  // namespace VoltMod
