#pragma once

#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <CS2Kit/Core/Subscription.hpp>
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

    /** Register @p callback for as long as the returned subscription lives. */
    [[nodiscard]] Subscription Listen(Callback callback)
    {
        const uint64_t id = _changed.Add(std::move(callback));
        return {[this](uint64_t handle) { _changed.Remove(handle); }, id};
    }

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
