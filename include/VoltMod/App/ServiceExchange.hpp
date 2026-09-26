#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
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
    /** @p services is the host's table; it outlives the exchange. */
    explicit ServiceExchange(IHostServices& services) : _services(services) {}

    /** Offer @p impl while the returned Subscription lives. Name @p T explicitly,
     *  `Publish<IBanService>(&_bans)`, so the stored pointer is the interface subobject the consumer
     *  casts back to. Keep the Subscription below @p impl, so the entry goes first. */
    template <class T>
    [[nodiscard]] Subscription Publish(T* impl)
    {
        return PublishNamed(T::InterfaceName, static_cast<void*>(impl));
    }

    /** Offer @p impl as one of several @p T, told apart by @p key: `Publish<IMenuSection>(this, "admin")`. */
    template <class T>
    [[nodiscard]] Subscription Publish(T* impl, std::string_view key)
    {
        return PublishNamed(KeyedName(T::InterfaceName, key), static_cast<void*>(impl));
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
    Subscription PublishNamed(std::string_view iface, void* impl);
    void* Find(std::string_view iface) const;

    static std::string KeyedName(std::string_view iface, std::string_view key)
    {
        return std::string(iface) + ":" + std::string(key);
    }

    IHostServices& _services;
};

}  // namespace VoltMod
