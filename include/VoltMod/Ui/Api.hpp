#pragma once

// The Ui module's public surface: spawning a custom_hud_layout, driving it, and the button
// presses coming back. Runtime holds the CustomUi service by value, so `<VoltMod/Api.hpp>`
// already reaches these types; include this header where a translation unit means to use them.

#include <VoltMod/Ui/UiClicks.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
