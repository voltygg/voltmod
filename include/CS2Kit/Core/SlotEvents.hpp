#pragma once

#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <cstdint>
#include <functional>

namespace CS2Kit::Core
{

/**
 * @brief "The occupant of this slot changed" - raised by PlayerManager, consumed below it.
 *
 * Lives in Core, not Players, so engine-level services can drop per-slot state without
 * depending on the player roster: Sdk sits under Players, and InputHistoryService and
 * TeleportTracker only ever needed the signal, never the Player objects.
 *
 * Fires on AddPlayer, RemovePlayer, and once per tracked slot on Clear.
 */
class SlotEvents
{
public:
    using Callback = std::function<void(int slot)>;

    /** Register @p callback; returns a handle for Remove. */
    uint64_t Listen(Callback callback) { return _changed.Add(std::move(callback)); }

    /** Unregister by handle. Safe with an unknown or zero id. */
    void Remove(uint64_t id) { _changed.Remove(id); }

    /** Notify every listener that @p slot changed hands. */
    void Raise(int slot)
    {
        for (const auto& [id, callback] : _changed.Items())
            callback(slot);
    }

private:
    CallbackRegistry<Callback> _changed;
};

}  // namespace CS2Kit::Core
