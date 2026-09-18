#pragma once

#include <VoltMod/Host/HostTypes.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace VoltMod
{

/**
 * @internal
 * @brief One host event's callbacks, in dispatch order and safe to edit from inside a dispatch.
 *
 * Order is the owning plugin's load position first and its own subscription order within that, so
 * a plugin loaded later never runs before one loaded earlier whatever order the two subscribe in.
 *
 * A pass in flight neither moves nor drops entries: a removal only marks, so the pass skips a
 * callback an earlier one removed, and a new subscription waits aside so it does not run in the
 * pass that created it. Both land when the outermost pass ends.
 */
template <class Fn>
class EventFanOut
{
public:
    void Add(HostToken token, uint64_t order, Fn call, void* context)
    {
        const Entry entry{.Token = token, .Order = order, .Call = call, .Context = context};
        if (_depth > 0)
            _pending.push_back(entry);
        else
            Insert(entry);
    }

    /** Whether this fan-out was the one holding @p token. */
    bool Remove(HostToken token)
    {
        for (Entry& entry : _entries)
        {
            if (entry.Token != token || entry.Removed)
                continue;

            entry.Removed = true;
            if (_depth == 0)
                Settle();
            return true;
        }

        const auto waiting = std::ranges::find(_pending, token, &Entry::Token);
        if (waiting == _pending.end())
            return false;

        _pending.erase(waiting);
        return true;
    }

    bool Empty() const { return _entries.empty() && _pending.empty(); }

    /**
     * Invoke @p visit(call, context) over the callbacks present when the pass began, stopping at
     * the first that returns true. Returns whether one did.
     */
    template <class Visit>
    bool Dispatch(Visit&& visit)
    {
        ++_depth;
        bool stopped = false;
        // Nothing is inserted or erased while the depth is up, so the count and the references hold.
        const size_t count = _entries.size();
        for (size_t i = 0; i < count && !stopped; ++i)
        {
            const Entry& entry = _entries[i];
            if (!entry.Removed)
                stopped = visit(entry.Call, entry.Context);
        }
        if (--_depth == 0)
            Settle();
        return stopped;
    }

private:
    struct Entry
    {
        HostToken Token = 0;
        uint64_t Order = 0;  ///< the owning plugin's load position
        Fn Call = nullptr;
        void* Context = nullptr;
        bool Removed = false;
    };

    void Insert(const Entry& entry)
    {
        const auto at = std::ranges::find_if(_entries, [&](const Entry& held) { return held.Order > entry.Order; });
        _entries.insert(at, entry);
    }

    /** Apply what the last pass had to defer. */
    void Settle()
    {
        std::erase_if(_entries, [](const Entry& entry) { return entry.Removed; });
        for (const Entry& entry : _pending)
            Insert(entry);
        _pending.clear();
    }

    std::vector<Entry> _entries;
    std::vector<Entry> _pending;
    int _depth = 0;
};

}  // namespace VoltMod
