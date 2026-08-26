#pragma once

#include <string>
#include <unordered_map>

namespace VoltMod
{

/**
 * @brief Typed interface exchange between separately-loaded plugins.
 *
 * The framework is a static library, so each plugin has its own Runtime and sharing cannot go
 * through framework state. It goes through Metamod instead: Publish fills this module's table,
 * which MetamodPlugin::OnMetamodQuery serves, and Get asks MetaFactory across every
 * loaded plugin.
 *
 * An interface is a pure-virtual struct carrying a versioned `InterfaceName`. Bump the
 * version in the name whenever the vtable or a parameter's meaning changes, so a stale
 * consumer gets nullptr rather than a mismatched vtable.
 *
 * Each plugin compiles its own memoverride.cpp and so has its own operator new: never
 * transfer ownership across the boundary. Take string_view, return trivially-copyable
 * types, let no exception escape.
 */
class ServiceExchange
{
public:
    /**
     * Offer @p impl under `T::InterfaceName` until Unpublish or unload.
     *
     * Name @p T explicitly (`Publish<IBanService>(&_bans)`) so the stored pointer is the
     * interface subobject the consumer casts back to.
     */
    template <class T>
    void Publish(T* impl)
    {
        PublishNamed(T::InterfaceName, static_cast<void*>(impl));
    }

    /** Publish under a runtime name, for interfaces keyed by something the type cannot
     *  carry (the per-plugin identity). Prefer Publish<T>. */
    void PublishNamed(const char* iface, void* impl) { _published[iface] = impl; }

    template <class T>
    void Unpublish()
    {
        UnpublishNamed(T::InterfaceName);
    }

    void UnpublishNamed(const char* iface) { _published.erase(iface); }

    /** What another plugin published for @p T, or nullptr. Not cached: peers come and go,
     *  and callers are rare enough that a factory walk each time is cheaper than tracking
     *  invalidation. */
    template <class T>
    T* Get() const
    {
        return static_cast<T*>(Query(T::InterfaceName));
    }

    /** Get() for a runtime name; the caller owns the cast. */
    void* GetNamed(const char* iface) const { return Query(iface); }

    /** This module's own entry for @p iface. Serves OnMetamodQuery; inline so the table
     *  can be tested without linking Metamod. */
    void* Find(const char* iface) const
    {
        auto it = _published.find(iface);
        return it == _published.end() ? nullptr : it->second;
    }

private:
    static void* Query(const char* iface);

    std::unordered_map<std::string, void*> _published;
};

}  // namespace VoltMod
