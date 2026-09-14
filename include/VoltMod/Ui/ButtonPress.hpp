#pragma once

#include <string>

namespace VoltMod
{

/**
 * @brief A `Button` press inside a custom HUD layout.
 *
 * Raised on the game frame after it arrives, not from inside inbound message processing, so a
 * handler may write to the layout or any other entity.
 */
struct ButtonPress
{
    int Slot = -1;         ///< the client that pressed, whatever pawn it is watching
    std::string ButtonId;  ///< the Button's `id` attribute; client-controlled text
};

}  // namespace VoltMod
