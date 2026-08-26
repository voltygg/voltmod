// Compile-only check: <VoltMod/Database/Api.hpp> compiles as the only VoltMod include in its
// translation unit. Only built when VOLTMOD_ENABLE_POSTGRES is on (see CMakeLists.txt);
// nothing here is ever called, so a passing build is the whole test.

#include <VoltMod/Database/Api.hpp>

void VoltmodApiSurface_DatabaseLinks(VoltMod::PostgresDatabase& database)
{
    (void)database;
}
