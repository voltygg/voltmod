#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace VoltMod
{

/**
 * @brief Workshop addons connecting clients must download.
 *
 * For content only the client uses (Panorama layouts, models, sounds); nothing is mounted on the
 * server. A client downloads one addon per reconnect - see @ref workshop_guide.
 *
 * ```cpp
 * if (auto required = runtime.Addons.Require(3401234567))
 *     _addon = std::move(*required);   // required until this Subscription drops
 * else
 *     Log::Warn("addons unavailable: {}", required.error().Detail);
 * ```
 *
 * Several plugins may require addons: a plugin's hook leaves alone a message an earlier plugin
 * already pointed at an addon. @ref Downloaded, @ref Missing and the retry settings cover this
 * plugin's requirements only. Game thread only.
 */
class Addons
{
public:
    /** All four must outlive this service. */
    Addons(Interfaces& interfaces, const Bindings& bindings, PlayerManager& players, Scheduler& scheduler);
    ~Addons();
    Addons(const Addons&) = delete;
    Addons& operator=(const Addons&) = delete;

    /**
     * Require @p id of every client until the returned Subscription drops. Reference counted;
     * connected clients are not disturbed and pick it up on their next connect.
     *
     * @return @ref ErrorCode::Invalid for id 0; @ref ErrorCode::Unsupported on a listen server or
     *         when the hook could not install.
     */
    [[nodiscard]] Result<Subscription> Require(uint64_t id);

    /** Like @ref Require, for the client with @p steamId only. */
    [[nodiscard]] Result<Subscription> RequireFor(int64_t steamId, uint64_t id);

    /** What every client must have, in send order. */
    std::vector<uint64_t> Required() const;

    /** What @p slot has still to download. */
    std::vector<uint64_t> Missing(int slot) const;

    /** Whether @p slot has anything left to download, without building the list. */
    [[nodiscard]] bool HasMissing(int slot) const;

    /** A client connected with every addon this plugin requires. Fires again after a reconnect
     *  caused by another plugin's addon. */
    Event<int /*slot*/> Downloaded;

    /** How soon a client must reconnect for its addon to count as downloaded. */
    double DownloadTimeoutSeconds = 30.0;

    /** How often one addon is offered to a client before it is dropped, so a client that declines
     *  does not reconnect forever. */
    int MaxDownloadAttempts = 3;

private:
    /** Hook on the first requirement; unhook once nothing is required. */
    Status EnsureHook();
    void UnhookIfUnused();

    void OnConnected(Player& player);
    void OnJoinMessage(const CNetMessage* message, void* client);

    /** Kicking inside the send hook crashes on Windows, so it waits a tick. */
    void KickLater(int slot, int64_t steamId);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    PlayerManager& _players;
    Scheduler& _scheduler;

    std::unique_ptr<AddonDownloads> _downloads;

    Subscription _connectListener;
    PerSlot<Subscription> _pendingKick;  ///< one queued kick per slot; queuing again replaces it
    Subscription _hook;
};

}  // namespace VoltMod
