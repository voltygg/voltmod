#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Ui/Screen.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <optional>
#include <string>

namespace VoltMod
{

/**
 * @brief One layout's player screens, each created the first time something draws for that slot.
 *
 * A player screen costs an entity, so this makes one per player who is actually shown the layout
 * rather than one per connected player. A screen removes its entity when its slot changes hands
 * and respawns for the next player on @ref Screen::EnsureSpawned.
 */
class PlayerScreens
{
public:
    /** @p screens must outlive this. */
    PlayerScreens(ScreenManager& screens, std::string layout);

    /** @p slot's screen, created on first use. One @ref ScreenManager::ForPlayer refuses is logged
     *  once and stays empty, so its writes fail quietly. */
    Screen& For(int slot);

    /** @p slot's screen if one was created, or null. Never creates. */
    [[nodiscard]] Screen* Find(int slot);

private:
    ScreenManager& _screens;
    std::string _layout;
    PerSlot<std::optional<Screen>> _created;
    /** Handed out for a slot that is not a player slot. */
    Screen _empty;
};

}  // namespace VoltMod
