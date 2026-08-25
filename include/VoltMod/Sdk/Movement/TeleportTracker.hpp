#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <cstdint>

class QAngle;
class Vector;

namespace VoltMod::Sdk
{

class EntitySystem;
class GameData;
class GameEventService;
class ServerClock;

/**
 * @brief Opt-in record of when each player's pawn was last teleported.
 *
 * Dormant until Enable(): it then hooks CBaseEntity::Teleport (gamedata offset "Teleport") on every
 * live pawn and stamps the server clock whenever one fires. A pawn is a fresh object after
 * every respawn, so the hook is re-bound on PlayerSpawn - and a spawn stamps a teleport of its own.
 *
 * The point is to discount the frame after a teleport, where origin and view angles jump
 * discontinuously and read as impossible movement to anything measuring motion.
 *
 * @code
 * runtime.Teleports.Enable();
 * if (!runtime.Teleports.JustTeleported(slot, 5.0f)) EvaluateAim(slot);
 * @endcode
 */
class TeleportTracker
{
public:
    /** @p entities resolves each slot's pawn, @p gameData supplies the Teleport vtable index,
     *  @p events the PlayerSpawn re-bind, @p clock the stamps. @p slots tells this tracker when a
     *  slot changes hands, without it needing the roster. All five must outlive it; the Runtime
     *  declares them above. */
    TeleportTracker(EntitySystem& entities, GameData& gameData, GameEventService& events, ServerClock& clock,
                    Core::SlotEvents& slots)
        : _entities(entities), _gameData(gameData), _events(events), _clock(clock), _slots(slots)
    {}
    ~TeleportTracker();
    TeleportTracker(const TeleportTracker&) = delete;
    TeleportTracker& operator=(const TeleportTracker&) = delete;

    /** Start tracking and bind every live pawn. Idempotent; false when the gamedata offset is missing. */
    bool Enable();

    /** Unbind every pawn and stop listening. Stamps are dropped. */
    void Disable();

    bool Enabled() const { return _enabled; }

    /** True when @p slot teleported within the last @p seconds of server time. */
    bool JustTeleported(int slot, float seconds) const;

    /** Drop every binding and stamp for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

private:
    void Hook_Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity);

    /** Rebind @p slot to its current pawn (no-op without one), replacing any previous binding. */
    void Bind(int slot);
    void Unbind(int slot);
    void Stamp(int slot);
    int SlotFromPawn(const void* pawn) const;

    EntitySystem& _entities;
    GameData& _gameData;
    GameEventService& _events;
    ServerClock& _clock;
    Core::SlotEvents& _slots;
    std::array<void*, Core::MaxPlayers> _pawns{};  // the instance each slot's hook is bound to
    std::array<int, Core::MaxPlayers> _hookIds{};  // SourceHook ids, 0 when unbound
    std::array<float, Core::MaxPlayers> _lastTeleport{};
    Core::Subscription _spawnListener;
    Core::Subscription _slotListener;
    bool _enabled = false;
};

}  // namespace VoltMod::Sdk
