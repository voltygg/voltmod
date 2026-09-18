#include <VoltMod/App/ServiceExchange.hpp>

namespace VoltMod
{

void ServiceExchange::PublishNamed(std::string_view iface, void* impl)
{
    if (_services)
        _services->Publish(iface, impl);
}

void ServiceExchange::UnpublishNamed(std::string_view iface)
{
    if (_services)
        _services->Unpublish(iface);
}

void* ServiceExchange::Find(std::string_view iface) const
{
    return _services ? _services->Find(iface) : nullptr;
}

}  // namespace VoltMod
