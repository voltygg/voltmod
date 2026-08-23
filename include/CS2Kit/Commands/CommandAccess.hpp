#pragma once

#include <CS2Kit/Commands/CommandManager.hpp>

namespace CS2Kit::Commands
{

/** Set/clear the active CommandManager. Called by the composition root on Load/Unload. */
void SetActiveCommands(CommandManager* commands);

/** The registered-command table. Commands-layer code uses this instead of the
 *  composition root. */
CommandManager& Manager();

/** The table, or nullptr - for teardown paths that may run after Shutdown. */
CommandManager* ManagerOrNull();

}  // namespace CS2Kit::Commands
