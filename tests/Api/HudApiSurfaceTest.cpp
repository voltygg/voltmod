// Compile-only check: <VoltMod/Hud/Api.hpp> compiles as the only VoltMod include in its
// translation unit. Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing
// here is ever called, so a passing build is the whole test.

#include <VoltMod/Hud/Api.hpp>

void VoltmodApiSurface_HudLinks(VoltMod::Hud& hud)
{
    (void)hud;
}
