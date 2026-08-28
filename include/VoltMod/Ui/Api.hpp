#pragma once

// The Ui module's public surface: a panel that owns its `custom_hud_layout` entity, and the button
// presses coming back from it. Runtime holds the CustomUi service by value, so `<VoltMod/Api.hpp>`
// already reaches these types; include this header where a translation unit means to use them.
//
// The click hook, the write cache and the row driver behind them live under src/ and are not part
// of this surface - which is what keeps VtableHook.hpp out of every consumer.

#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
