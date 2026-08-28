// Compile-only check: <VoltMod/Ui/Api.hpp> compiles as the only VoltMod include in its
// translation unit, and reaches the whole Ui surface - a panel, the service that spawns one, and
// the press coming back. Built as part of voltmod-api-surface-check (see CMakeLists.txt); nothing
// here is ever called, so a passing build is the whole test.

#include <VoltMod/Ui/Api.hpp>

void VoltmodApiSurface_UiLinks(VoltMod::UiPanel& panel, VoltMod::CustomUi& ui, const VoltMod::UiClick& click)
{
    (void)panel;
    (void)ui;
    (void)click;
}
