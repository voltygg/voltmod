#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/**
 * @brief Workshop addons that connecting clients are told to download.
 *
 * A CS2 client fetches one addon per connection cycle, so a client needing several reconnects
 * once per addon. This drives that: each time the server sends a client its signon message, the
 * message is rewritten to name the next addon that client is still missing, which makes the
 * client download it and reconnect. When the list runs out the client joins normally and
 * @ref Ready fires.
 *
 * ### What this does not do
 *
 * Nothing is downloaded or mounted **on the server**. This is for content only the client
 * renders - Panorama layouts (see @ref CustomHud), models, sounds. Content the server itself
 * needs must be installed and mounted the usual way, and a workshop map still goes through
 * `Map::ChangeToWorkshop`.
 *
 * ### Cost, and the one guess it makes
 *
 * Each addon costs the joining client one reconnect, including the first: the server's own addon
 * string is left alone, so the extras only ride the signon cycle.
 *
 * Nothing tells the server that a download finished. Like the plugin this is modelled on, a
 * client is taken to have got the addon it was last sent if it reconnects within
 * @ref DownloadTimeoutSeconds; a client that declines is dropped rather than left reconnecting
 * forever. Raising the timeout suits slow connections or large addons.
 *
 * Inert on a listen server (there is no download step) and when @ref Capability::Addons is off.
 * Everything here runs on the game thread.
 */
class Addons
{
public:
    /** All four must outlive this service; the Runtime declares them above it. */
    Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler);
    ~Addons();
    Addons(const Addons&) = delete;
    Addons& operator=(const Addons&) = delete;

    /** Require @p id of every client from now on. Idempotent; already-connected clients are not
     *  disturbed, so a change takes effect on their next connect. */
    void Require(uint64_t id);

    /** Stop requiring @p id. */
    void Drop(uint64_t id);

    /** Stop requiring anything. */
    void Clear();

    /** What every client is required to have, in the order they are sent. */
    std::span<const uint64_t> Required() const { return _required; }

    /** Require @p id of one player only, on top of the global list. Keyed by SteamID because a
     *  client cycling through downloads has no slot yet. */
    void RequireFor(int64_t steamId, uint64_t id);

    /** Stop requiring @p id of @p steamId. */
    void DropFor(int64_t steamId, uint64_t id);

    /** Addons @p slot has still to fetch. Empty once it is fully loaded. */
    std::vector<uint64_t> Pending(int slot) const;

    /** A client finished the last addon it was missing and is joining normally. */
    Event<int /*slot*/> Ready;

    /** How soon a client must reconnect for its last addon to count as downloaded. Raise it for
     *  large addons or slow connections; a client returning later than this starts over. */
    double DownloadTimeoutSeconds = 30.0;

    /** How many times one addon is offered to the same client before it is dropped. This is the
     *  loop breaker: a client that declines the download otherwise reconnects forever. */
    int MaxDownloadAttempts = 3;

private:
    /** One connecting client's progress through its addon list. */
    struct ClientState
    {
        std::vector<uint64_t> Extra;       ///< required of this SteamID alone
        std::vector<uint64_t> Downloaded;  ///< confirmed by a reconnect
        uint64_t Sending = 0;              ///< sent but not yet confirmed
        double SentAt = 0.0;               ///< when Sending was set, for the timeout
        int Attempts = 0;                  ///< offers of Sending so far, against MaxDownloadAttempts
    };

    /** Install on the first requirement, remove when nothing is required. */
    void Arm();
    void Disarm();

    /** Everything @p steamId must have: the global list, then its own. */
    std::vector<uint64_t> RequiredFor(int64_t steamId) const;
    /** ...minus what it has already downloaded. */
    std::vector<uint64_t> MissingFor(int64_t steamId) const;

    void OnConnected(Player& player);
    bool Hook_SendNetMessage(const CNetMessage* message, int bufType);

    /** Queue @p slot to be dropped next frame; see @ref _kick. */
    void KickLater(int slot);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    PlayerManager& _players;
    Scheduler& _scheduler;

    std::vector<uint64_t> _required;
    std::unordered_map<int64_t, ClientState> _clients;

    Subscription _connectListener;
    /** Slots waiting to be dropped, and the one-shot that drains them. Kicking from inside the
     *  SendNetMessage hook crashes on Windows, so it happens a frame later. */
    std::vector<int> _kickSlots;
    Subscription _kick;
    VtableHook _hook;
};

}  // namespace VoltMod
