#pragma once

#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace VoltMod::Core
{

/**
 * @brief Handle-keyed item store shared by every listener registry in the framework.
 *
 * Scheduler timers, ConVar change listeners, and game-event listeners all need the same
 * thing: add an item, get back a stable `uint64_t` handle, remove by handle later. This
 * is that one implementation. Handles start at 1 and never repeat within a Load/Unload
 * cycle, so 0 is free to mean "no registration".
 *
 * Iteration order is unspecified (unordered_map). Use @ref Dispatch (or @ref DispatchIf) to
 * fire callbacks - it handles the case where one of them adds or removes registrations mid-loop.
 * @ref Items is for inspection only.
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
    [[nodiscard]] Subscription AddOwned(T item)
    {
        const uint64_t id = Add(std::move(item));
        return Subscription([this, id] { Remove(id); });
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

    /** Direct view for range-for; pairs of (handle, item). Prefer @ref Dispatch when the loop
     *  body can reach back into the registry. */
    const std::unordered_map<uint64_t, T>& Items() const { return _items; }

    /**
     * Invoke @p fn(item) for every stored item @p pred accepts, safe against a callback that adds
     * or removes registrations while it runs - including one that drops its own Subscription.
     *
     * Handles are snapshotted first, then re-found one at a time, because invoking rehashes or
     * erases and would otherwise invalidate a live iterator. The item is copied out before the
     * call for the same reason: running a callback can destroy the stored copy of itself. @p pred
     * is applied to the stored item, so entries it rejects never pay for that copy - which is what
     * lets a registry keyed by something other than the handle (game events by name) filter here
     * rather than hand-rolling its own snapshot. Registries here hold a handful of entries at
     * most, so the snapshot stays on the stack unless it has to grow.
     */
    template <class Pred, class Fn>
    void DispatchIf(Pred&& pred, Fn&& fn)
    {
        if (_items.empty())
            return;

        constexpr size_t InlineCapacity = 8;
        std::array<uint64_t, InlineCapacity> inlineIds{};
        std::vector<uint64_t> overflowIds;
        size_t count = 0;
        for (const auto& [id, item] : _items)
        {
            if (!pred(item))
                continue;
            if (count < InlineCapacity)
                inlineIds[count] = id;
            else
                overflowIds.push_back(id);
            ++count;
        }

        for (size_t i = 0; i < count; ++i)
        {
            T* stored = Find(i < InlineCapacity ? inlineIds[i] : overflowIds[i - InlineCapacity]);
            if (!stored || !pred(*stored))
                continue;  // an earlier callback in this batch removed it
            T item = *stored;
            fn(item);
        }
    }

    /** @ref DispatchIf over every stored item. */
    template <class Fn>
    void Dispatch(Fn&& fn)
    {
        DispatchIf([](const T&) { return true; }, std::forward<Fn>(fn));
    }

private:
    std::unordered_map<uint64_t, T> _items;
    uint64_t _nextId = 1;
};

}  // namespace VoltMod::Core
