#pragma once

#include <CS2Kit/Core/SlotEvents.hpp>
#include <CS2Kit/Core/Subscription.hpp>
#include <CS2Kit/Players/Player.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CS2Kit::Players
{

/**
 * @brief Manages all connected players, indexed by slot and SteamID.
 *
 * Main-thread-only (no mutex) - all access happens from game-thread callbacks.
 * Plugins call AddPlayer/RemovePlayer from their connect/disconnect hooks.
 */
class PlayerManager
{
public:
    using SlotCallback = Core::SlotEvents::Callback;

    /** @p slots is the Core-level signal this manager raises; it outlives the manager. */
    explicit PlayerManager(Core::SlotEvents& slots) : _slots(slots) {}

    Player* AddPlayer(int slot, int64_t steamId, const std::string& name, const std::string& ipAddress);
    void RemovePlayer(int slot);
    void Clear();

    /**
     * Fires whenever a slot's occupant changes: on AddPlayer, RemovePlayer, and
     * once per tracked slot on Clear. The backing hook for per-slot state that
     * must not leak across occupants (see PerSlot).
     *
     * A convenience over Core::SlotEvents for plugin code that already holds a
     * PlayerManager. Anything below Players in the layering listens on the signal
     * itself instead - that is the whole reason it lives in Core.
     */
    [[nodiscard]] Core::Subscription ListenSlotChange(SlotCallback callback)
    {
        return _slots.Listen(std::move(callback));
    }

    Player* GetPlayerBySlot(int slot);
    Player* GetPlayerBySteamId(int64_t steamId);

    /**
     * The player in @p slot, but only if their SteamID is still @p steamId. Use wherever a slot
     * was captured earlier - a menu step, a queued database completion, a scheduled task - since
     * the original player may have left and the slot may now host somebody else. Returns null in
     * that case, rather than the wrong player.
     */
    Player* GetPlayerBySlotIfSteamId(int slot, int64_t steamId);
    std::vector<Player*> FindPlayersByName(const std::string& name);
    std::vector<Player*> GetAllPlayers();
    size_t GetPlayerCount() const;

private:
    void IndexBySteamId(Player* player);
    void UnindexBySteamId(const Player* player);
    void FireSlotChange(int slot);

    Core::SlotEvents& _slots;
    std::unordered_map<int, std::unique_ptr<Player>> _playersBySlot;
    std::unordered_map<int64_t, Player*> _playersBySteamId;
};

}  // namespace CS2Kit::Players
