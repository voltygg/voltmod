#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <cassert>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Per-player-slot value store that never leaks state across occupants.
 *
 * A plain `std::array<T, MaxPlayers>` plus an optional binding to the slot-change
 * feed: after BindReset() the entry for a slot is value-reset whenever a player
 * joins or leaves it. Default construction is inert, so PerSlot can live in plugin
 * manager containers and bind later - typically `_state.BindReset(runtime.Slots)`
 * from the owner's constructor or Initialize.
 *
 * Takes @ref SlotEvents rather than the runtime so a plugin translation unit
 * that includes only this header still compiles.
 */
template <class T>
class PerSlot
{
public:
    PerSlot() = default;
    /** Unsubscribes directly, so the feed cannot reset entries that are going away. */
    ~PerSlot() { _listener.Reset(); }
    PerSlot(const PerSlot&) = delete;
    PerSlot& operator=(const PerSlot&) = delete;

    /** Auto-reset a slot's entry on player connect/disconnect. Idempotent; @p slots must
     *  outlive this object. */
    void BindReset(SlotEvents& slots)
    {
        if (!_listener)
            _listener = slots.Listen([this](int slot) { Reset(slot); });
    }

    /** @pre IsValidSlot(slot); asserted, not checked - callers that can receive an unvalidated
     *  slot (console callers, CallerSlot() == -1) must check IsValidSlot before indexing. */
    T& operator[](int slot)
    {
        assert(IsValidSlot(slot));
        return _items[slot];
    }
    const T& operator[](int slot) const
    {
        assert(IsValidSlot(slot));
        return _items[slot];
    }

    void Reset(int slot)
    {
        if (IsValidSlot(slot))
            _items[slot] = T{};
    }

    void ResetAll() { _items.fill(T{}); }

private:
    std::array<T, MaxPlayers> _items{};
    Subscription _listener;
};

}  // namespace VoltMod
