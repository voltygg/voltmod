#pragma once

#include <CS2Kit/Menu/MenuManager.hpp>

namespace CS2Kit::Menu
{

/** Set/clear the active MenuManager. Called by the composition root on Load/Unload. */
void SetActiveMenus(MenuManager* menus);

/** The open-menu registry. Menu-layer code uses this instead of the composition root. */
MenuManager& Menus();

/** The registry, or nullptr - for teardown paths that may run after Shutdown. */
MenuManager* MenusOrNull();

}  // namespace CS2Kit::Menu
