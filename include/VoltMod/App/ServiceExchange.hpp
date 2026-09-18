#pragma once

#include <VoltMod/Host/HostTypes.hpp>
#include <VoltMod/Host/IHostServices.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Typed interface exchange between separately-loaded plugins.
 *
 * The host owns one table for the process. Publish puts this plugin's implementation in it under
 * `T::InterfaceName` and Get asks for whatever any plugin published under that name.
 *
 * An interface is a pure-virtual struct carrying a versioned `InterfaceName`. Bump the version in
 * the name whenever the vtable or a parameter's meaning changes, so a stale consumer gets nullptr
 * rather than a mismatched vtable.
 *
 * Each plugin compiles its own memoverride.cpp and so has its own operator new: never transfer
 * ownership across the boundary. Take string_view, return trivially-copyable types, let no
 * exception escape.
 */
class ServiceExchange
{
public:
    /** Attach to the host's table. Called by Plugin::Attach before OnLoad. */
    void Attach(IHostServices* services) { _services = services; }

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

    void PublishNamed(std::string_view iface, void* impl);

    template <class T>
    void Unpublish()
    {
        UnpublishNamed(T::InterfaceName);
    }

    void UnpublishNamed(std::string_view iface);

    /** What any plugin published for @p T, or nullptr. Not cached: peers come and go, and the
     *  host never loads or unloads one inside a callback, so a pointer fetched at the point of
     *  use cannot dangle before you are done with it. */
    template <class T>
    T* Get() const
    {
        return static_cast<T*>(Find(T::InterfaceName));
    }

    /** The raw entry for @p iface, or nullptr. */
    void* Find(std::string_view iface) const;

private:
    IHostServices* _services = nullptr;
};

}  // namespace VoltMod
