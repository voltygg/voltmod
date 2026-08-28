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
 * @brief A multicast signal with a fixed handler signature: the one way to subscribe in VoltMod.
 *
 * The owner declares the event as a public member and is the only caller of @ref Raise; everyone
 * else adds a handler with `+=` and keeps the returned @ref Subscription beside the state the
 * handler captured:
 *
 * @code
 * _spawnSub = runtime.Slots.Changed += [this](int slot) { _cache.Reset(slot); };
 * @endcode
 *
 * A handler is never invoked after its Subscription drops, including from inside the @ref Raise
 * that is already running.
 *
 * **Lifetime.** The Subscription holds a raw pointer to the event, so the event must outlive it.
 * Declaring the subscription as a member of the object that owns the handler's state gives that
 * for free; a subscription stored above the service it points at does not.
 *
 * **Lazy install.** An event whose source costs something to run - a vtable hook, an engine-wide
 * callback - is constructed with a @ref Lifecycle. @ref Lifecycle::OnFirst runs before the first
 * handler is stored and may refuse (returning false yields an empty Subscription and stores
 * nothing); @ref Lifecycle::OnLast runs after the last handler is removed. Nothing else installs
 * the source, so subscribing is the only trigger and the last unsubscribe is the only teardown.
 *
 * Not copyable or movable: subscriptions point at one address for their whole life.
 */
template <class... Args>
class Event
{
public:
    using Handler = std::function<void(Args...)>;

    /**
     * @brief Engine-side install/remove driven by whether anything is listening.
     *
     * @ref OnFirst returns false to reject the subscription; the owner is expected to have said
     * why (a log line today, a capability record later). Both run on the game thread, and
     * @ref OnLast may run from inside @ref Raise when the last handler drops itself.
     */
    struct Lifecycle
    {
        std::function<bool()> OnFirst;
        std::function<void()> OnLast;
    };

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
