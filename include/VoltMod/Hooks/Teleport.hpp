#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <array>

namespace VoltMod
{

/**
 * @brief Raises @ref Teleported whenever a player's pawn is moved by CBaseEntity::Teleport.
 *
 * Dormant until something subscribes: it then hooks the "Teleport" vtable index (from gamedata) on
 * every live pawn, and dropping the last subscription unbinds them all again. A pawn is a fresh
 * object after every respawn, so the hook is re-bound on PlayerSpawn - and since a spawn also
 * moves the player, **a spawn raises the event too**. Filter spawns yourself if you only care
 * about mid-life teleports.
 *
 * The hook is all this owns. It keeps no history: a teleport breaks continuity - origin and view
 * angles jump discontinuously, so anything measuring motion across ticks reads the following frame
 * as impossible - and how long that window lasts, and in what clock, is the consumer's question,
 * not this service's.
 *
 * @code
 * _teleports = runtime.Teleports.Teleported += [this](int slot) { _lastTeleport[slot] = _clock.Time(); };
 * @endcode
 */
class Teleport
{
public:
    /** @p entities resolves each slot's pawn, @p bindings supplies the Teleport vtable index,
     *  @p events the PlayerSpawn re-bind. @p slots tells this tracker when a slot changes hands,
     *  without it needing the roster. All four must outlive it; the Runtime declares them above. */
    Teleport(EntitySystem& entities, const Bindings& bindings, GameEvents& events, SlotEvents& slots);
    ~Teleport();
    Teleport(const Teleport&) = delete;
    Teleport& operator=(const Teleport&) = delete;

    /** A pawn was teleported; the argument is its slot (-1 when it belongs to no player).
     *  Subscribing arms the tracker. */
    Event<int> Teleported;

    /** Drop every binding for the new map. Called by the framework's StartupServer hook. */
    void OnServerStartup();

private:
    Status Enable();
    void Disable();

    void Hook_Teleport(const Vector* origin, const QAngle* angles, const Vector* velocity);

    /** Rebind @p slot to its current pawn (no-op without one), replacing any previous binding. */
    void Bind(int slot);
    void Unbind(int slot);
    int SlotFromPawn(const void* pawn) const;

    EntitySystem& _entities;
    const Bindings& _bindings;
    GameEvents& _events;
    SlotEvents& _slots;
    std::array<void*, MaxPlayers> _pawns{};  // the instance each slot's hook is bound to
    std::array<int, MaxPlayers> _hookIds{};  // SourceHook ids, 0 when unbound
    Subscription _spawnListener;
    Subscription _slotListener;
    bool _enabled = false;
};

}  // namespace VoltMod
