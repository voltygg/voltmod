#include <CS2Kit/Core/MetamodPluginBase.hpp>  // PLUGIN_GLOBALVARS -> g_SMAPI
#include <CS2Kit/Core/ServiceExchange.hpp>

namespace CS2Kit::Core
{

void* ServiceExchange::Query(const char* iface)
{
    if (!g_SMAPI)
        return nullptr;

    int ret = META_IFACE_FAILED;
    void* impl = g_SMAPI->MetaFactory(iface, &ret, nullptr);
    return ret == META_IFACE_OK ? impl : nullptr;
}

}  // namespace CS2Kit::Core
