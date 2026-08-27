#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace VoltMod
{

/**
 * @brief Workshop addons that connecting clients are told to download.
 *
 * A CS2 client fetches one addon per connection cycle, so a client needing several reconnects once
 * per addon - see @ref workshop_guide for how that is driven and what it costs. This is for
 * content only the client renders (Panorama layouts, models, sounds); nothing is downloaded or
 * mounted on the server.
 *
 * ```cpp
 * auto lease = runtime.Addons.Require(3401234567);
 * if (!lease)
 *     Log::Warn("addons unavailable: {}", lease.error().Detail);
 * else
 *     _addon = std::move(*lease);   // required until this Subscription drops
 * ```
 *
 * **One plugin should own the addon list.** The framework is a static library, so each plugin has
 * its own Runtime, its own instance of this and its own hook; two plugins requiring different
 * addons rewrite the same message and only one wins.
 *
 * Inert on a listen server (there is no download step) and when @ref Capability::Addons is off;
 * @ref Require reports either as an error rather than silently doing nothing. Everything here runs
 * on the game thread.
 */
class Addons
{
public:
    /** All four must outlive this service; the Runtime declares them above it. */
    Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler);
    ~Addons();
    Addons(const Addons&) = delete;
    Addons& operator=(const Addons&) = delete;

    /**
     * Require @p id of every client until the returned Subscription drops.
     *
     * Requirements are reference counted, so two callers may require the same addon independently.
     * Already-connected clients are not disturbed; a change takes effect on their next connect.
     *
     * @return the lease, or why nothing was required: @ref ErrorCode::Invalid for id 0,
     *         @ref ErrorCode::Unsupported on a listen server or when the hook could not install.
     */
    [[nodiscard]] Result<Subscription> Require(uint64_t id);

    /** @copydoc Require. Of one client only, on top of the global list. Keyed by SteamID because
     *  a client cycling through downloads has no stable slot. */
    [[nodiscard]] Result<Subscription> RequireFor(int64_t steamId, uint64_t id);

    /** What every client is required to have, in the order they are sent. */
    std::vector<uint64_t> Required() const;

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
    /** Install on the first requirement, remove when nothing is required. */
    Status Arm();
    void Disarm();

    void OnConnected(Player& player);
    bool Hook_SendNetMessage(const CNetMessage* message, int bufType);

    /** The hook's actual work, so the hook itself is one unconditional MRES_IGNORED. */
    void HandleSignon(const CNetMessage* message, void* client);

    /** Drop @p slot next frame, if @p steamId still holds it; see @ref _pendingKick. */
    void KickLater(int slot, int64_t steamId);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    PlayerManager& _players;
    Scheduler& _scheduler;

    /** Requirements and per-client progress, defined under src/: a plugin has no business
     *  reaching the table, and it keeps this header free of it. */
    class Impl;
    std::unique_ptr<Impl> _impl;

    Subscription _connectListener;
    /** One pending drop per slot. Kicking from inside the SendNetMessage hook crashes on Windows,
     *  so it happens a frame later; re-arming a slot cancels the one-shot already queued for it. */
    PerSlot<Subscription> _pendingKick;
    VtableHook _hook;
};

}  // namespace VoltMod
