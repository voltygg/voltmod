#include <VoltMod/App/ServiceExchange.hpp>
#include <string>

namespace VoltMod
{

Subscription ServiceExchange::PublishNamed(std::string_view iface, void* impl)
{
    _services.Publish(iface, impl);
    return Subscription([&services = _services, name = std::string(iface)] { services.Unpublish(name); });
}

void* ServiceExchange::Find(std::string_view iface) const
{
    return _services.Find(iface);
}

}  // namespace VoltMod
