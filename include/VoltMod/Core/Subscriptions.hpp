#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <utility>
#include <vector>

namespace VoltMod
{

/**
 * @brief Holds the subscriptions an object makes and releases them together, newest first.
 *
 * @code
 * _subs.Add(runtime.Slots.Changed += [this](int slot) { Forget(slot); });
 * _subs.Add(runtime.GameEvents.On<PlayerSpawn>([this](const PlayerSpawn& e) { Spawn(e.Slot); }));
 * @endcode
 *
 * Declare it after whatever its handlers touch, the same as a single Subscription. A handler that
 * has to be dropped on its own still wants its own Subscription member.
 */
class Subscriptions
{
public:
    Subscriptions() = default;
    ~Subscriptions() { Clear(); }

    Subscriptions(Subscriptions&&) noexcept = default;
    /** Releases what this held before taking @p other's. */
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

    /** Keep @p subscription for as long as this lives. */
    void Add(Subscription subscription) { _items.push_back(std::move(subscription)); }

    /** Release everything now, newest first. */
    void Clear()
    {
        while (!_items.empty())
            _items.pop_back();
    }

    /** True while this holds nothing - also how a caller asks "have I subscribed yet?" for a
     *  registration deferred to first use. */
    [[nodiscard]] bool Empty() const noexcept { return _items.empty(); }

private:
    std::vector<Subscription> _items;
};

}  // namespace VoltMod
