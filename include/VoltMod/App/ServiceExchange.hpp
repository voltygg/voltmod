#pragma once

#include <VoltMod/Host/IHostServices.hpp>
#include <string>
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

    /** Offer @p impl as one of several @p T, told apart by @p key: `Publish<IMenuSection>(this, "admin")`. */
    template <class T>
    void Publish(T* impl, std::string_view key)
    {
        PublishNamed(KeyedName(T::InterfaceName, key), static_cast<void*>(impl));
    }

    template <class T>
    void Unpublish()
    {
        UnpublishNamed(T::InterfaceName);
    }

    template <class T>
    void Unpublish(std::string_view key)
    {
        UnpublishNamed(KeyedName(T::InterfaceName, key));
    }

    /** What any plugin published for @p T, or nullptr. Ask where you use it and do not keep it:
     *  the publisher can unload between callbacks, never inside one. */
    template <class T>
    T* Get() const
    {
        return static_cast<T*>(Find(T::InterfaceName));
    }

    /** What a plugin published for @p T under @p key, or nullptr. */
    template <class T>
    T* Get(std::string_view key) const
    {
        return static_cast<T*>(Find(KeyedName(T::InterfaceName, key)));
    }

private:
    void PublishNamed(std::string_view iface, void* impl);
    void UnpublishNamed(std::string_view iface);
    void* Find(std::string_view iface) const;

    static std::string KeyedName(std::string_view iface, std::string_view key)
    {
        return std::string(iface) + ":" + std::string(key);
    }

    IHostServices* _services = nullptr;
};

}  // namespace VoltMod
