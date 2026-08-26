#include <VoltMod/App/MetamodPlugin.hpp>  // PLUGIN_GLOBALVARS -> g_SMAPI
#include <VoltMod/App/ServiceExchange.hpp>

namespace VoltMod
{

void* ServiceExchange::Query(const char* iface)
{
    if (!g_SMAPI)
        return nullptr;

    int ret = META_IFACE_FAILED;
    void* impl = g_SMAPI->MetaFactory(iface, &ret, nullptr);
    return ret == META_IFACE_OK ? impl : nullptr;
}

}  // namespace VoltMod
