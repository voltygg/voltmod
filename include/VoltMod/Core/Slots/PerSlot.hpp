#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <array>
#include <cassert>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief Per-player-slot value store that never leaks state across occupants.
 *
 * A plain `std::array<T, MaxPlayers>`. Constructed with the slot-change feed, the entry for a
 * slot is value-reset whenever a player joins or leaves it: `PerSlot<State> _state{runtime.Slots};`.
 * Default construction is inert, for an owner that resets entries itself.
 *
 * Takes @ref SlotEvents rather than the runtime so a plugin translation unit
 * that includes only this header still compiles.
 */
template <class T>
class PerSlot
{
public:
    PerSlot() = default;
    /** Resets a slot's entry on player connect/disconnect. @p slots must outlive this object. */
    explicit PerSlot(SlotEvents& slots) : _listener(slots.Changed += [this](int slot) { Reset(slot); }) {}
    /** Unsubscribes directly, so the feed cannot reset entries that are going away. */
    ~PerSlot() { _listener.Reset(); }
    PerSlot(const PerSlot&) = delete;
    PerSlot& operator=(const PerSlot&) = delete;

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

    /** Assigned one by one rather than through `fill`, which would copy: the common T here is
     *  move-only (a Subscription), and dropping one is exactly what cancels it. */
    void ResetAll()
    {
        for (T& item : _items)
            item = T{};
    }

private:
    std::array<T, MaxPlayers> _items{};
    Subscription _listener;
};

}  // namespace VoltMod
