#include <VoltMod/App/ServiceExchange.hpp>

namespace VoltMod
{

static HostString Borrow(std::string_view text)
{
    return HostString{.Data = text.data(), .Length = text.size()};
}

void ServiceExchange::PublishNamed(std::string_view iface, void* impl)
{
    if (_services)
        _services->Publish(Borrow(iface), impl);
}

void ServiceExchange::UnpublishNamed(std::string_view iface)
{
    if (_services)
        _services->Unpublish(Borrow(iface));
}

void* ServiceExchange::Find(std::string_view iface) const
{
    return _services ? _services->Find(Borrow(iface)) : nullptr;
}

}  // namespace VoltMod
