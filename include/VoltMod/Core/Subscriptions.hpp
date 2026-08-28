#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief A bag of @ref Subscription, for an object that subscribes to several things at once.
 *
 * @ref On subscribes and keeps the registration in one line, so a handful of handlers is one
 * member rather than one member each:
 *
 * @code
 * _subs.On(runtime.Slots.Changed, [this](int slot) { Forget(slot); });
 * _subs.On(_panel.Button("accept"), [this](int slot) { Accept(slot); });
 * @endcode
 *
 * Everything in the bag is released when it is destroyed or cleared, in the reverse of the order
 * it went in. Declare it after whatever its handlers touch, the same as a single Subscription.
 *
 * A handler that has to be dropped on its own still wants its own @ref Subscription member; this
 * is for the ones that live and die together.
 */
class Subscriptions
{
public:
    Subscriptions() = default;
    ~Subscriptions() { Clear(); }

    Subscriptions(Subscriptions&&) noexcept = default;
    /** Releases what this bag held before taking @p other's. */
    Subscriptions& operator=(Subscriptions&& other) noexcept
    {
        if (this != &other)
        {
            Clear();
            _items = std::move(other._items);
        }
        return *this;
    }

    Subscriptions(const Subscriptions&) = delete;
    Subscriptions& operator=(const Subscriptions&) = delete;

    /** Subscribe @p handler to @p event and keep the registration for as long as this bag lives. */
    template <class... Args, class Handler>
    void On(Event<Args...>& event, Handler&& handler)
    {
        _items.push_back(event += std::forward<Handler>(handler));
    }

    /** Keep a registration made somewhere else: a @ref Scheduler timer, a hook install. */
    void Add(Subscription&& subscription) { _items.push_back(std::move(subscription)); }

    /** Release everything now, newest first. */
    void Clear()
    {
        while (!_items.empty())
            _items.pop_back();
    }

    /** True while the bag holds nothing - which is also how a caller asks "have I subscribed
     *  yet?" for a registration deferred to first use. */
    [[nodiscard]] bool Empty() const noexcept { return _items.empty(); }

private:
    std::vector<Subscription> _items;
};

}  // namespace VoltMod
