#pragma once

#include <CS2Kit/Players/PlayerManager.hpp>

namespace CS2Kit::Players
{

/** Set/clear the active PlayerManager. Called by the composition root on Load/Unload. */
void SetActiveRoster(PlayerManager* roster);

/** The connected-player roster. Asserts if called outside a Load/Unload window.
 *  Players-layer code uses this instead of reaching up to the composition root. */
PlayerManager& Roster();

/** The roster, or nullptr - for teardown paths that may run after Shutdown. */
PlayerManager* RosterOrNull();

}  // namespace CS2Kit::Players
