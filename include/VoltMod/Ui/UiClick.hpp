#pragma once

#include <VoltMod/Entities/EntityRef.hpp>
#include <string>

namespace VoltMod
{

/**
 * @brief A `Button` press inside a custom HUD layout.
 *
 * Raised on the game frame after the press arrives rather than from inside the engine's inbound
 * message processing, so a handler may write to the layout or any other entity.
 */
struct UiClick
{
    int Slot = -1;         ///< who clicked
    EntityRef Layout;      ///< the custom_hud_layout the Button belongs to, already resolved
    std::string ButtonId;  ///< the Button's `id` attribute; client-controlled text
};

}  // namespace VoltMod
