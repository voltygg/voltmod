// Compile-only check: <VoltMod/Api.hpp> alone satisfies a translation unit that wants the
// core vocabulary, the Runtime facade, and player/command/plugin plumbing - and never reaches
// nlohmann or the Menu-building surface. See docs/getting-started.md's Api header table for
// what belongs here versus in Entities/Hooks/Menu/Unsafe/Database's own Api.hpp.
//
// Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing here is ever
// called, so a passing build is the whole test.

#include <VoltMod/Api.hpp>

#ifdef NLOHMANN_JSON_VERSION_MAJOR
#error "<VoltMod/Api.hpp> must not reach nlohmann - route JSON-backed config through <VoltMod/App/Config.hpp>"
#endif

#ifdef VOLTMOD_MENU_FLOW_HPP
#error "<VoltMod/Api.hpp> must not reach <VoltMod/Menu/Flow.hpp> - menu building is <VoltMod/Menu/Api.hpp>"
#endif

// A reference parameter is enough to prove Runtime is a complete, usable type here.
void VoltmodApiSurface_RootLinks(VoltMod::Runtime& runtime)
{
    (void)runtime;
}
