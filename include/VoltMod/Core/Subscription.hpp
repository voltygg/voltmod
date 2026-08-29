#pragma once

#include <functional>
#include <utility>

namespace VoltMod
{

/**
 * @brief Owns one registration and releases it on destruction.
 *
 * Holding one keeps the registration alive. Store it beside the state captured by its handler so
 * it unregisters before that state is destroyed:
 *
 * @code
 * class Bhop {
 *     Bhop(GameEvents& events) {
 *         _spawn = events.On<PlayerSpawn>([this](const PlayerSpawn& e) { OnSpawn(e.Slot); });
 *     }
 *     Subscription _spawn;
 * };
 * @endcode
 *
 * The cleanup callable may hold any registry handle, including non-handle-based registrations
 * such as SourceHook installs (see VOLTMOD_SCOPED_HOOK).
 *
 * Move-only. The registry must outlive its Subscription; reverse declaration order provides this
 * when both are members of one object.
 *
 * `<VoltMod/Core/Subscriptions.hpp>` holds several of them in one member, for an object that
 * subscribes to a handful of things at once.
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

}  // namespace VoltMod
