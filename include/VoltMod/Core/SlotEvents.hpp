#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstdint>
#include <functional>

namespace VoltMod::Core
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
    [[nodiscard]] Subscription Listen(Callback callback) { return _changed.AddOwned(std::move(callback)); }

    /** Notify every listener that @p slot changed hands. */
    void Raise(int slot)
    {
        _changed.Dispatch([slot](auto& callback) { callback(slot); });
    }

private:
    CallbackRegistry<Callback> _changed;
};

}  // namespace VoltMod::Core
