#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod::Internal
{

// The two things a row does to the session it is drawn in. They live behind this seam because
// MenuHost.hpp reaches the engine (and through it the SDK), while everything else about a row -
// its labels, its stepping, its input validation - is plain values: keeping these two calls in
// their own translation unit is what lets MenuBuilder.cpp compile, and be tested, without either.
// MenuHost is forward-declared in Engine/EngineTypes.hpp (reached through Menu.hpp).

/** @ref MenuHost::OpenMenu, so a submenu row can push what its factory built. */
void OpenMenu(MenuHost& menus, int slot, std::shared_ptr<Menu> menu);

/** @ref MenuHost::BeginInput, so an input row can route the player's next chat line to
 *  @p callback under @p prompt. */
void BeginInput(MenuHost& menus, int slot, std::string prompt, std::function<bool(int, std::string_view)> callback);

}  // namespace VoltMod::Internal
