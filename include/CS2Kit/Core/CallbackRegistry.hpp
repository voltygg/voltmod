#pragma once

#include <CS2Kit/Core/Subscription.hpp>
#include <cstdint>
#include <unordered_map>

namespace CS2Kit::Core
{

/**
 * @brief Handle-keyed item store shared by every listener registry in the kit.
 *
 * Scheduler timers, ConVar change listeners, and game-event listeners all need the same
 * thing: add an item, get back a stable `uint64_t` handle, remove by handle later. This
 * is that one implementation. Handles start at 1 and never repeat within a Load/Unload
 * cycle, so 0 is free to mean "no registration".
 *
 * Iteration order is unspecified (unordered_map). Callers that fire callbacks while
 * iterating should snapshot handles first if a callback may mutate the registry.
 */
template <class T>
class CallbackRegistry
{
public:
    /** Store @p item and return its handle. */
    uint64_t Add(T item) { return Add(std::move(item), _nextId++); }

    /** Store @p item under a caller-supplied @p id - for owners sharing one handle space across several registries. */
    uint64_t Add(T item, uint64_t id)
    {
        _items.emplace(id, std::move(item));
        return id;
    }

    /**
     * Store @p item and return a Subscription that removes it on destruction - the owning
     * form of Add, for registries whose handles callers would otherwise have to hand back.
     */
    [[nodiscard]] Subscription AddOwned(T item) { return AddOwned(std::move(item), _nextId++); }

    /** AddOwned under a caller-supplied @p id, for owners sharing one handle space. */
    [[nodiscard]] Subscription AddOwned(T item, uint64_t id)
    {
        return {[this](uint64_t handle) { Remove(handle); }, Add(std::move(item), id)};
    }

    /** Remove by handle. Safe to call with an unknown id; returns whether anything was removed. */
    bool Remove(uint64_t id) { return _items.erase(id) > 0; }

    void Clear() { _items.clear(); }

    bool Empty() const { return _items.empty(); }

    /** The stored item, or nullptr if the handle is gone. Pointer invalidated by Add/Remove. */
    T* Find(uint64_t id)
    {
        auto it = _items.find(id);
        return it != _items.end() ? &it->second : nullptr;
    }

    /** Direct view for range-for; pairs of (handle, item). */
    const std::unordered_map<uint64_t, T>& Items() const { return _items; }

private:
    std::unordered_map<uint64_t, T> _items;
    uint64_t _nextId = 1;
};

}  // namespace CS2Kit::Core
