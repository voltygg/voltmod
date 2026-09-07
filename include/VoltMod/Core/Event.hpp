#pragma once

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace VoltMod
{

/**
 * @brief Starting and stopping an @ref Event source, driven by whether anything is listening.
 *
 * @ref OnFirst runs before the first handler is stored; false refuses the subscription, and the
 * owner is expected to have said why. @ref OnLast runs after the last handler is removed. Both run
 * on the game thread, and OnLast may run from inside @ref Event::Raise when the last handler drops
 * itself.
 */
struct EventLifecycle
{
    std::function<bool()> OnFirst;
    std::function<void()> OnLast;
};

/**
 * @brief A multicast signal with a fixed handler signature: the one way to subscribe in VoltMod.
 *
 * The owner declares it as a public member and is the only caller of @ref Raise. Everyone else
 * adds a handler with `+=` and keeps the returned @ref Subscription beside the state that handler
 * captured:
 *
 * @code
 * _spawnSub = runtime.Slots.Changed += [this](int slot) { _cache.Reset(slot); };
 * @endcode
 *
 * A handler never runs after its Subscription drops, not even from inside a @ref Raise already
 * under way.
 *
 * **Lifetime.** The Subscription points at the event, so the event has to outlive it. Declaring
 * the subscription in the object that owns the handler state gives that for free; storing it above
 * the service it points at does not.
 *
 * **Lazy install.** An event whose source costs something to run - a vtable hook, an engine-wide
 * callback - takes an @ref EventLifecycle. Subscribing starts the source and dropping the last
 * subscription stops it; nothing else installs it. Several events fed by one source share a
 * @ref SharedSource.
 *
 * Not copyable or movable: subscriptions point at one address for their whole life.
 */
template <class... Args>
class Event
{
public:
    using Handler = std::function<void(Args...)>;
    /** @ref EventLifecycle, reachable as `Event<...>::Lifecycle` where that reads better. */
    using Lifecycle = EventLifecycle;

    Event() = default;
    explicit Event(Lifecycle lifecycle) : _lifecycle(std::move(lifecycle)) {}

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    /** Subscribe @p handler for as long as the returned Subscription lives. An empty Subscription
     *  means nothing was stored: @p handler was empty, or a @ref Lifecycle refused. */
    [[nodiscard]] Subscription operator+=(Handler handler)
    {
        if (!handler)
            return {};

        if (_handlers.Empty() && _lifecycle.OnFirst && !_lifecycle.OnFirst())
            return {};

        const uint64_t id = _handlers.Add(std::move(handler));
        return Subscription([this, id] {
            if (_handlers.Remove(id) && _handlers.Empty() && _lifecycle.OnLast)
                _lifecycle.OnLast();
        });
    }

    bool Empty() const noexcept { return _handlers.Empty(); }
    size_t Count() const noexcept { return _handlers.Items().size(); }

    /**
     * Invoke every handler. The owner raises; a consumer with a `+=` subscription does not.
     *
     * Re-entrancy safe: a handler may subscribe, unsubscribe another, or drop its own
     * Subscription while this runs. One added during the raise first fires on the next one.
     */
    void Raise(Args... args)
    {
        _handlers.Dispatch([&](Handler& handler) { handler(args...); });
    }

private:
    CallbackRegistry<Handler> _handlers;
    Lifecycle _lifecycle;
};

}  // namespace VoltMod
