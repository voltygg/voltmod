#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/PerSlot.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
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
 * For client-only content (Panorama layouts, models, sounds); the server mounts nothing. One addon
 * downloads per reconnect - see @ref workshop_guide.
 *
 * ```cpp
 * if (auto required = runtime.Addons.Require(3401234567))
 *     _addon = std::move(*required);   // required until this Subscription drops
 * else
 *     Log::Warn("addons unavailable: {}", required.error().Detail);
 * ```
 *
 * Plugins share the hooks safely. @ref Downloaded, @ref Missing and the retry settings cover this
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
     * Require @p id of every client until the Subscription drops. Reference counted; connected
     * clients pick it up on their next connect.
     *
     * @return @ref ErrorCode::Invalid for id 0; @ref ErrorCode::Unsupported on a listen server or
     *         when the hooks could not install.
     */
    [[nodiscard]] Result<Subscription> Require(uint64_t id);

    /** Like @ref Require, for the client with @p steamId only. */
    [[nodiscard]] Result<Subscription> RequireFor(int64_t steamId, uint64_t id);

    /** What every client must have, in send order. */
    std::vector<uint64_t> Required() const;

    /** What @p slot has still to download. */
    std::vector<uint64_t> Missing(int slot) const;

    /** Whether @p slot has anything left to download, without building the list. */
    bool HasMissing(int slot) const;

    /** A client connected with every addon this plugin requires. May fire again on a later reconnect. */
    Event<int /*slot*/> Downloaded;

    /** How soon a client must reconnect for its addon to count as downloaded. */
    double DownloadTimeoutSeconds = 30.0;

    /** Offers of one addon before a declining client is dropped. */
    int MaxDownloadAttempts = 3;

private:
    /** Hook on the first requirement; unhook once nothing is required. */
    Status InstallHooks();
    void RemoveHooksIfUnused();

    void OnConnected(Player& player);
    void OnJoinMessage(const CNetMessage* message, void* client);

    /** Add @p client's addons to the server's list for its connection reply, then remove them. */
    void AddToReply(CNetworkGameServerBase& server, const EngineClient* client);
    void RestoreReply(CNetworkGameServerBase& server);

    /** Waits a tick: kicking inside the send hook crashes on Windows. */
    void KickLater(int slot, int64_t steamId);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    PlayerManager& _players;
    Scheduler& _scheduler;

    std::unique_ptr<AddonDownloads> _downloads;

    Subscription _connectListener;
    PerSlot<Subscription> _pendingKick;  ///< at most one queued kick per slot
    Subscription _joinMessageHook;       ///< tells a client what to download
    Subscription _connectionReplyHook;   ///< tells a client what to mount
    std::vector<uint64_t> _addedToReply;
};

}  // namespace VoltMod
