#pragma once

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Ui/UiPanels.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One generated layout, owned: the panel it draws on and whether its root is hidden.
 *
 * Either shared - one panel everyone sees - or private, a panel per player made on first use, so
 * a spectating admin sees their own screen rather than the one belonging to the pawn they watch.
 *
 * @ref Show re-ensures the panel on every call, which is what recovers from a map change: the
 * panel goes empty then, and the next Show re-spawns it rather than needing a reconnect handler.
 * Repeat calls cost nothing - the panel's write cache drops the unhide it has already sent.
 */
class Screen
{
public:
    /** One panel for everyone. A @ref UiPanels::Panel failure is logged here, leaving an empty
     *  panel that fails every later @ref Show the same way. */
    Screen(UiPanels& ui, std::string_view layout, std::string_view rootId);

    /** A panel per player, spawned on first @ref Show. @p slots drops a departing player's panel
     *  so it does not leak into whoever takes the slot next. */
    Screen(UiPanels& ui, SlotEvents& slots, std::string_view layout, std::string_view rootId);

    /** Make the layout exist for @p slot and unhide its root; @p capture also gives that slot the
     *  cursor. False when it cannot be shown - the panel already logged why. */
    bool Show(int slot, bool capture = false);

    /** Hide the root for @p slot and drop its cursor. Keeps the entity and its other state. */
    void Hide(int slot);

    /** The panel @p slot draws on. Empty when it could not be spawned. */
    UiPanel& Panel(int slot = EveryoneSlot);

    [[nodiscard]] std::string_view Layout() const noexcept;

private:
    UiPanels& _ui;
    std::string _layout;
    std::string _root;
    /** True when each player draws on their own panel. */
    bool _perPlayer;
    UiPanel _shared;
    PerSlot<UiPanel> _private;
};

}  // namespace VoltMod
