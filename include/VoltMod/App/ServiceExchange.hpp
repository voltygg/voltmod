#pragma once

#include <VoltMod/Host/IHostServices.hpp>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Typed view of the host's table of interfaces plugins publish to each other.
 *
 * An interface is a pure-virtual struct with a versioned `InterfaceName` ("bans.IBanService/2").
 * Bump the version whenever the vtable or a parameter's meaning changes, so a stale consumer gets
 * nullptr instead of a mismatched vtable. Each plugin has its own allocator: never transfer
 * ownership through an interface, and let no exception escape one.
 */
class ServiceExchange
{
public:
    /** Attach to the host's table before the plugin is constructed. */
    void Attach(IHostServices* services) { _services = services; }

    /** Offer @p impl until Unpublish or unload. Name @p T explicitly, `Publish<IBanService>(&_bans)`,
     *  so the stored pointer is the interface subobject the consumer casts back to. */
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

    /** What any plugin published for @p T, or nullptr. Ask where you use it and do not keep it:
     *  the publisher can unload between callbacks, never inside one. */
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
