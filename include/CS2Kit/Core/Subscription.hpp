#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace CS2Kit::Core
{

/**
 * @brief Owns one listener registration and removes it on destruction.
 *
 * Every registry in the kit hands out a `uint64_t` handle, which callers then had to
 * remember to hand back - usually from a destructor that first checked whether the
 * service was still alive. That check was the bug: by the time those destructors ran the
 * ambient accessor was already cleared, so the removal silently never happened.
 *
 * Holding the subscription instead makes the lifetime the compiler's problem. Declare it
 * as a member next to whatever the callback captures, and it unregisters before that
 * state goes away:
 *
 * @code
 * class Bhop {
 *     Bhop(GameEventService& events) {
 *         _spawn = events.Listen<Events::PlayerSpawn>([this](const auto& e) { OnSpawn(e.Slot); });
 *     }
 *     Subscription _spawn;
 * };
 * @endcode
 *
 * Move-only, and safe to destroy after the registry it points at is gone only if the
 * registry outlives it - which reverse-declaration-order destruction gives you for free
 * when both are members of the same object.
 */
class Subscription
{
public:
    using Remover = std::function<void(uint64_t)>;

    Subscription() = default;

    /** Take ownership of @p id, to be passed to @p remove on destruction. */
    Subscription(Remover remove, uint64_t id) : _remove(std::move(remove)), _id(id) {}

    ~Subscription() { Reset(); }

    Subscription(Subscription&& other) noexcept : _remove(std::move(other._remove)), _id(std::exchange(other._id, 0))
    {
        other._remove = nullptr;
    }

    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _remove = std::move(other._remove);
            _id = std::exchange(other._id, 0);
            other._remove = nullptr;
        }
        return *this;
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /** Unregister now. Idempotent; the subscription is empty afterwards. */
    void Reset()
    {
        if (_remove && _id != 0)
            _remove(_id);
        _remove = nullptr;
        _id = 0;
    }

    /** Give up ownership without unregistering, returning the raw handle. */
    uint64_t Release() noexcept
    {
        _remove = nullptr;
        return std::exchange(_id, 0);
    }

    /** True while this holds a live registration. */
    explicit operator bool() const noexcept { return _id != 0; }

private:
    Remover _remove;
    uint64_t _id = 0;
};

}  // namespace CS2Kit::Core
