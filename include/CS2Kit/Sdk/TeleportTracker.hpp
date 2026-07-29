#pragma once

#include <CS2Kit/Core/Slot.hpp>
#include <array>
#include <cstdint>

class QAngle;
class Vector;

namespace CS2Kit::Sdk
{

/**
 * @brief Opt-in record of when each player's pawn was last teleported.
 *
 * Dormant until Enable(): it then hooks CBaseEntity::Teleport (gamedata offset "Teleport") on every
 * live pawn and stamps @ref Sdk::ServerTime() whenever one fires. A pawn is a fresh object after
 * every respawn, so the hook is re-bound on PlayerSpawn - and a spawn stamps a teleport of its own.
 *
 * The point is to discount the frame after a teleport, where origin and view angles jump
 * discontinuously and read as impossible movement to anything measuring motion.
 *
 * @code
 * Engine().Teleports.Enable();
 * if (!Engine().Teleports.JustTeleported(slot, 5.0f)) EvaluateAim(slot);
 * @endcode
 */
class TeleportTracker
{
public:
    TeleportTracker() = default;
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

    /** Drop every binding and stamp for the new map. Called by the kit's StartupServer hook. */
    void OnServerStartup();

private:
    void Hook_Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity);

    /** Rebind @p slot to its current pawn (no-op without one), replacing any previous binding. */
    void Bind(int slot);
    void Unbind(int slot);
    void Stamp(int slot);
    int SlotFromPawn(const void* pawn) const;

    std::array<void*, Core::MaxPlayers> _pawns{};  // the instance each slot's hook is bound to
    std::array<int, Core::MaxPlayers> _hookIds{};  // SourceHook ids, 0 when unbound
    std::array<float, Core::MaxPlayers> _lastTeleport{};
    uint64_t _spawnListener = 0;
    uint64_t _slotListener = 0;
    bool _enabled = false;
};

}  // namespace CS2Kit::Sdk
