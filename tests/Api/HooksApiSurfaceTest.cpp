// Compile-only check: <VoltMod/Hooks/Api.hpp> compiles as the only VoltMod include in its
// translation unit. Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing
// here is ever called, so a passing build is the whole test.

#include <VoltMod/Hooks/Api.hpp>

void VoltmodApiSurface_HooksLinks(VoltMod::Movement& movement)
{
    (void)movement;
}
