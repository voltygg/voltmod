#pragma once

#include <functional>
#include <utility>

namespace CS2Kit::Core
{

/**
 * @brief Owns one registration and releases it on destruction.
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
 * The handle stays inside the cleanup callable, so this works just as well for the
 * add/remove pairs that are not handle-keyed at all, such as SourceHook installs (see
 * CS2KIT_SCOPED_HOOK).
 *
 * Move-only, and safe to destroy after the registry it points at is gone only if the
 * registry outlives it - which reverse-declaration-order destruction gives you for free
 * when both are members of the same object.
 */
class Subscription
{
public:
    Subscription() = default;

    /** Run @p cleanup on destruction; it captures whatever handle the registry issued. */
    explicit Subscription(std::function<void()> cleanup) : _cleanup(std::move(cleanup)) {}

    ~Subscription() { Reset(); }

    Subscription(Subscription&& other) noexcept : _cleanup(std::exchange(other._cleanup, nullptr)) {}

    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            _cleanup = std::exchange(other._cleanup, nullptr);
        }
        return *this;
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /** Release now. Idempotent, and re-entrant: the callable is detached before it runs. */
    void Reset()
    {
        if (auto cleanup = std::exchange(_cleanup, nullptr))
            cleanup();
    }

    /** True while this holds a live registration. */
    explicit operator bool() const noexcept { return static_cast<bool>(_cleanup); }

private:
    std::function<void()> _cleanup;
};

}  // namespace CS2Kit::Core
