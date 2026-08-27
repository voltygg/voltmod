#pragma once

// The Hud module's public surface: spawning a custom_hud_layout, driving it, and the button
// presses coming back. Runtime holds the CustomHud service by value, so `<VoltMod/Api.hpp>`
// already reaches these types; include this header where a translation unit means to use them.

#include <VoltMod/Hud/Hud.hpp>
#include <VoltMod/Hud/HudClicks.hpp>
