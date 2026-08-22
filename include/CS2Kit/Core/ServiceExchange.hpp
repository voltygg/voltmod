#pragma once

#include <string>
#include <unordered_map>

namespace CS2Kit::Core
{

/**
 * @brief Typed interface exchange between separately-loaded plugins.
 *
 * cs2-kit is a static library, so every plugin module owns its own copy of the kit's
 * globals - @ref Engine() in one plugin is a different Services than in another. Sharing
 * therefore cannot go through kit state; it goes through Metamod, which is the one thing
 * both modules genuinely share. Publish writes into this module's table, which
 * `MetamodPluginBase::OnMetamodQuery` serves; Get() asks `ISmmAPI::MetaFactory`, which
 * walks every loaded plugin.
 *
 * An interface is any pure-virtual struct carrying its own name:
 *
 * @code
 * struct IBanService
 * {
 *     static constexpr const char* InterfaceName = "cs2plugins.IBanService/1";
 *     virtual BanResult Ban(int64_t steamId, int64_t seconds, std::string_view reason) = 0;
 * protected:
 *     ~IBanService() = default;
 * };
 * @endcode
 *
 * Version the *name*, not the object: any change to the vtable layout or to a parameter's
 * meaning gets a new `/2` suffix, so a stale consumer sees a clean nullptr instead of
 * calling through a mismatched vtable.
 *
 * Because the two sides are separate modules with separate `operator new` (cs2_add_plugin
 * compiles memoverride.cpp per plugin), interfaces must never transfer ownership of memory
 * across the boundary: pass std::string_view, return trivially-copyable types, and let
 * exceptions cross nothing.
 */
class ServiceExchange
{
public:
    /**
     * Offer @p impl to other plugins under `T::InterfaceName`, until Unpublish or unload.
     *
     * Name @p T explicitly (`Publish<IBanService>(&_bans)`) so the stored pointer is the
     * interface subobject rather than the concrete type - the consumer casts back to @p T.
     */
    template <class T>
    void Publish(T* impl)
    {
        _published[T::InterfaceName] = static_cast<void*>(impl);
    }

    /** Withdraw `T::InterfaceName`. Publishing plugins should do this before their state dies. */
    template <class T>
    void Unpublish()
    {
        _published.erase(T::InterfaceName);
    }

    /**
     * The implementation another loaded plugin published for @p T, or nullptr when no plugin
     * offers it. Not cached: a peer can unload at any time, and callers are rare enough that
     * a factory walk per call is cheaper than tracking invalidation.
     */
    template <class T>
    T* Get() const
    {
        return static_cast<T*>(Query(T::InterfaceName));
    }

    /**
     * This module's own published pointer for @p iface, or nullptr. Serves OnMetamodQuery.
     * Inline so the table can be exercised without linking Metamod - only Query() needs it.
     */
    void* Find(const char* iface) const
    {
        auto it = _published.find(iface);
        return it == _published.end() ? nullptr : it->second;
    }

private:
    /** Ask Metamod's factory across all loaded plugins. */
    static void* Query(const char* iface);

    std::unordered_map<std::string, void*> _published;
};

}  // namespace CS2Kit::Core
