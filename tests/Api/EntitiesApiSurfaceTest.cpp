// Compile-only check: <VoltMod/Entities/Api.hpp> compiles as the only VoltMod include in its
// translation unit. Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing
// here is ever called, so a passing build is the whole test.

#include <VoltMod/Entities/Api.hpp>

void VoltmodApiSurface_EntitiesLinks(VoltMod::EntitySystem& entities)
{
    (void)entities;
}
