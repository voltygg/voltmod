#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <array>
#include <cstdint>

namespace VoltMod::Players
{

/**
 * @brief Per-player-slot value store that never leaks state across occupants.
 *
 * A plain `std::array<T, MaxPlayers>` plus an optional binding to the
 * PlayerManager slot-change feed: after BindReset() the entry for a slot is
 * value-reset whenever a player joins or leaves it. Call BindReset() once the
 * framework services are live (e.g. in a manager's Initialize); default construction
 * is inert so PerSlot can live in plugin manager containers.
 */
template <class T>
class PerSlot
{
public:
    PerSlot() = default;
    ~PerSlot() { Unbind(); }
    PerSlot(const PerSlot&) = delete;
    PerSlot& operator=(const PerSlot&) = delete;

    /** Auto-reset a slot's entry on player connect/disconnect. Idempotent. */
    void BindReset()
    {
        if (!_listener)
            _listener = VoltMod::Detail::Slots().Listen([this](int slot) { Reset(slot); });
    }

    void Unbind() { _listener.Reset(); }

    T& operator[](int slot) { return _items[slot]; }
    const T& operator[](int slot) const { return _items[slot]; }

    void Reset(int slot)
    {
        if (Core::IsValidSlot(slot))
            _items[slot] = T{};
    }

    void ResetAll() { _items.fill(T{}); }

    auto begin() { return _items.begin(); }
    auto end() { return _items.end(); }
    auto begin() const { return _items.begin(); }
    auto end() const { return _items.end(); }

private:
    std::array<T, Core::MaxPlayers> _items{};
    Core::Subscription _listener;
};

}  // namespace VoltMod::Players
