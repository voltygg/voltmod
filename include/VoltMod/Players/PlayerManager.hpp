#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/**
 * @brief The roster: every connected player, and the signals for the connection lifecycle.
 *
 * Main-thread-only (no mutex) - the engine callbacks that drive it and everything that reads it
 * run on the game thread. The framework owns the mutations; plugins look players up and
 * subscribe to the four events.
 *
 * A returned `Player*` is null when nobody matched, and lives only as long as that connection.
 * Keep a @ref PlayerRef instead of a pointer or a bare slot.
 */
class PlayerManager
{
public:
    /**
     * @p slots is the Core-level "this slot changed hands" signal this manager raises; subscribe
     *   on that feed (`runtime.Slots.Changed`) rather than here when all you need is to drop
     *   per-slot state - it lives in Core precisely so services below Players can hear it.
     * @p entities builds the wrappers @ref Player::Ctrl and @ref Player::GetPawn return. Null
     *   only in the framework's SDK-free unit tests, where there is no engine.
     * Both must outlive the manager.
     */
    PlayerManager(SlotEvents& slots, EntitySystem* entities) : _slots(slots), _entities(entities) {}

    PlayerManager(const PlayerManager&) = delete;
    PlayerManager& operator=(const PlayerManager&) = delete;

    /** @brief A player joined and is now in the roster. Their name is not meaningful yet -
     *  @ref FullyConnected is the first point it is. */
    Event<Player&> Connected;

    /** @brief A player is leaving. Raised while they are still in the roster, so the handler can
     *  read their identity and flush whatever it keyed on them; the @ref Player is destroyed
     *  immediately afterwards. */
    Event<Player&> Disconnected;

    /** @brief A player finished connecting (post ClientFullyConnect) - the first point their
     *  name and replicated convars mean anything. */
    Event<Player&> FullyConnected;

    /** @brief A player changed a replicated setting (name, userinfo cvars). Fires on every
     *  change, including the ones the engine sends at connect. */
    Event<Player&> SettingsChanged;

    /** The player in @p slot, or null when it is empty. */
    Player* Get(int slot);

    /**
     * The player @p ref names: the same slot **and** the same SteamID. Null when the slot is
     * empty or has changed hands, rather than the wrong player - which is the whole reason a
     * slot captured earlier is stored as a ref. A bot ref (`SteamId == 0`) only says "a bot is
     * still in that slot"; no reference can pin one down across a reconnect.
     */
    Player* Get(PlayerRef ref);

    /** The connected player with this SteamID, or null. Bots are never reachable this way -
     *  they all share SteamID 0. */
    Player* BySteamId(int64_t steamId);

    /**
     * Every connected player, in slot order. Backed by a vector the manager keeps up to date, so
     * this allocates nothing; it is invalidated by the next Add/Remove/Clear, which means a loop
     * over it must not connect or disconnect anybody.
     */
    [[nodiscard]] std::span<Player* const> All() const { return _ordered; }

    /** The ref for whoever occupies @p slot, or an unset ref when it is empty. The way a
     *  transient slot - a menu row, an engine callback - becomes a storable identity. */
    [[nodiscard]] PlayerRef RefFor(int slot);

    /** @internal Roster mutation and lifecycle raising belong to the framework's Metamod
     *  callbacks (`MetamodPlugin`); a plugin that calls these desynchronizes the roster from
     *  the engine. */
    /** @{ */
    Player* Add(int slot, int64_t steamId, std::string name, std::string ip);
    void Remove(int slot);
    /** Drops everyone, raising @ref Disconnected for each first. */
    void Clear();
    void OnClientFullyConnected(int slot);
    void OnClientSettingsChanged(int slot);
    /** @} */

private:
    void IndexBySteamId(Player* player);
    void UnindexBySteamId(const Player* player);
    /** Rebuild the slot-ordered view. Called by every mutation, which is per connect/disconnect
     *  rather than per lookup. */
    void Reindex();
    /** Raise Disconnected, unindex and erase @p slot's occupant. The shared body of Remove and
     *  of Add taking over an occupied slot. */
    void Drop(int slot);

    SlotEvents& _slots;
    EntitySystem* _entities;
    std::unordered_map<int, std::unique_ptr<Player>> _playersBySlot;
    std::unordered_map<int64_t, Player*> _playersBySteamId;
    std::vector<Player*> _ordered;
};

}  // namespace VoltMod
