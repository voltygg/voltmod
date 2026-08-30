#include <VoltMod/Api.hpp>

// Glaze publishes no version macro; this one comes from <glaze/core/opts.hpp>, which every glaze
// entry point pulls in.
#ifdef GLZ_NULL_TERMINATED
#error "<VoltMod/Api.hpp> must not reach glaze - route JSON-backed config through <VoltMod/App/Config.hpp>"
#endif

void VoltmodApiSurface_RootLinks(VoltMod::Runtime& runtime)
{
    (void)runtime;
}
