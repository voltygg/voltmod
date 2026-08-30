#include <VoltMod/Api.hpp>

// Glaze exposes no preprocessor version macro, so Core/Json.hpp sets this sentinel instead.
// It also catches a header that reaches Glaze without going through Core/Json.hpp at all,
// because such a header would be a layering violation in its own right.
#ifdef VOLTMOD_JSON_INCLUDED
#error "<VoltMod/Api.hpp> must not reach the JSON layer - route JSON-backed config through <VoltMod/App/Config.hpp>"
#endif

void VoltmodApiSurface_RootLinks(VoltMod::Runtime& runtime)
{
    (void)runtime;
}
