#pragma once

#include <VoltMod/Core/Event.hpp>

namespace VoltMod
{

/**
 * @brief "The occupant of this slot changed" - raised by PlayerManager, consumed below it.
 *
 * Lives in Core, not Players, so engine-level services can drop per-slot state without
 * depending on the player roster: Hooks sits under Players, and Teleport only ever needed
 * the signal, never the Player objects.
 *
 * Fires on AddPlayer, RemovePlayer, and once per tracked slot on Clear.
 */
class SlotEvents
{
public:
    /** `runtime.Slots.Changed += [](int slot) { ... };` */
    Event<int> Changed;

    /** Notify every handler that @p slot changed hands. PlayerManager's to call. */
    void Raise(int slot) { Changed.Raise(slot); }
};

}  // namespace VoltMod
