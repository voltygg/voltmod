#include <VoltMod/App/MetamodPlugin.hpp>  // PLUGIN_GLOBALVARS -> g_SMAPI
#include <VoltMod/App/ServiceExchange.hpp>
#include <string>

namespace VoltMod
{

void* ServiceExchange::Query(std::string_view iface)
{
    if (!g_SMAPI)
        return nullptr;

    // MetaFactory matches the name against every loaded plugin's table during the call.
    const std::string name(iface);
    int ret = META_IFACE_FAILED;
    void* impl = g_SMAPI->MetaFactory(name.c_str(), &ret, nullptr);
    return ret == META_IFACE_OK ? impl : nullptr;
}

}  // namespace VoltMod
