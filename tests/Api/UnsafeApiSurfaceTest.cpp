// Compile-only check: <VoltMod/Unsafe/Api.hpp> compiles as the only VoltMod include in its
// translation unit. Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing
// here is ever called, so a passing build is the whole test.

#include <VoltMod/Unsafe/Api.hpp>

void VoltmodApiSurface_UnsafeLinks(VoltMod::Bindings& bindings)
{
    (void)bindings;
}
