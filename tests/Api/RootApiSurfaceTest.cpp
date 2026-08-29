#include <VoltMod/Api.hpp>

#ifdef NLOHMANN_JSON_VERSION_MAJOR
#error "<VoltMod/Api.hpp> must not reach nlohmann - route JSON-backed config through <VoltMod/App/Config.hpp>"
#endif

void VoltmodApiSurface_RootLinks(VoltMod::Runtime& runtime)
{
    (void)runtime;
}
