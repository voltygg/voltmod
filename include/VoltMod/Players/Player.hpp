#pragma once

#include <VoltMod/Core/Time.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Players/PlayerRef.hpp>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief One connected player: the live identity behind a slot.
 *
 * A `Player` is owned by @ref PlayerManager for the length of one connection and keeps its
 * address while the player is connected, so a `Player&` handed to a callback stays valid for
 * that call. It dies on disconnect, on slot reuse, and on `Clear()` - store a @ref PlayerRef
 * across anything that can outlive the connection (a menu step, a queued database completion,
 * a scheduled task) and resolve it again with `runtime.Players.Get(ref)`.
 *
 * Identity only. Admin flags, punishments and statistics belong in plugin-side managers keyed
 * by SteamID, not on this type.
 *
 * @ref Name, @ref Ctrl and @ref GetPawn read the engine on every call rather than caching: the
 * scoreboard name changes mid-connection and the pawn is replaced on every spawn, so a cached
 * copy is wrong the moment it is taken.
 */
class Player
{
public:
    /**
     * @internal @ref PlayerManager builds these; plugins never construct one.
     *
     * @p connectName is what the engine reported at ClientConnected, which is only a fallback:
     * the name is not meaningful until ClientFullyConnect, so @ref Name prefers the controller.
     * @p entities is null only in the framework's SDK-free unit tests, where there is no engine;
     * the engine-facing accessors are then inert.
     */
    Player(int slot, int64_t steamId, std::string connectName, std::string ip, EntitySystem* entities)
        : _slot(slot),
          _steamId(steamId),
          _connectName(std::move(connectName)),
          _ip(std::move(ip)),
          _entities(entities),
          _connectTime(Time::Now())
    {}

    // One Player is one connection: copying would make a second identity for the same slot.
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    [[nodiscard]] int Slot() const noexcept { return _slot; }
    [[nodiscard]] int64_t SteamId() const noexcept { return _steamId; }

    /** True for engine bots, which connect without a real SteamID. */
    [[nodiscard]] bool IsBot() const noexcept { return _steamId == 0; }

    /** This player as a storable reference. */
    [[nodiscard]] PlayerRef Ref() const noexcept { return {.Slot = _slot, .SteamId = _steamId}; }

    /** The scoreboard name, read from the controller. Falls back to the connect-time name while
     *  there is no controller yet - between ClientConnected and the first spawn. */
    [[nodiscard]] std::string Name() const;

    /** The IP the player connected from, captured at connect because the engine offers it only
     *  there. Empty for bots. */
    [[nodiscard]] std::string_view Ip() const noexcept { return _ip; }

    /** How long this connection has lasted. */
    [[nodiscard]] std::chrono::seconds Playtime() const { return std::chrono::seconds{Time::Now() - _connectTime}; }

    /** @{ The engine wrappers for this player, resolved now. Frame-local: read them, do not
     *  store them. Falsy when the player has no controller or no pawn. Include
     *  <VoltMod/Entities/EntitySystem.hpp> (or <VoltMod/Api.hpp>) to use what they return. */
    [[nodiscard]] Controller Ctrl() const;
    [[nodiscard]] Pawn GetPawn() const;
    /** @} */

private:
    int _slot;
    int64_t _steamId;
    std::string _connectName;
    std::string _ip;
    /** The wrapper factory, owned by the Runtime that owns the roster. */
    EntitySystem* _entities;
    int64_t _connectTime;
};

}  // namespace VoltMod
