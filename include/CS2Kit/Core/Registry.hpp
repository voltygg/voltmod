#pragma once

#include <span>
#include <utility>
#include <vector>

namespace CS2Kit::Core
{

/**
 * @brief Per-module append-only registry for data-only descriptors (commands, effects, ...).
 *
 * Lets a descriptor register itself at its definition site instead of being push_back'd from
 * a central list:
 *
 * @code
 * static const bool _registered = CS2Kit::Registry<EffectEntry>::Add({...});
 * @endcode
 *
 * cs2-kit is a static library, so the registry static lives inside the plugin DLL: `meta unload`
 * discards it with the module and a reload re-runs the registrants - registrations are per-load
 * by construction. Constraint: items are constructed during static init, before Load, so they
 * must be data-only (they cannot reach the runtime at construction; lambdas that call it at invoke
 * time are fine).
 */
template <class T>
class Registry
{
public:
    static bool Add(T item)
    {
        MutableItems().push_back(std::move(item));
        return true;
    }

    /** Registration order == static-init order within a TU; across TUs it is unspecified, so
     *  sort by an explicit order field when presentation order matters. */
    static std::span<const T> Items() { return MutableItems(); }

private:
    static std::vector<T>& MutableItems()
    {
        static std::vector<T> items;
        return items;
    }
};

}  // namespace CS2Kit::Core
