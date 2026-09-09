#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief One generated layout, owned: its panel, whether each player has it up, and its buttons.
 *
 * Shared (@p viewer @ref UiPanel::Everyone) or private to one slot. @ref Show re-ensures the panel
 * on every call, which is what recovers from a map change: the panel goes falsy then, and the next
 * Show simply re-spawns it rather than needing a reconnect handler of its own.
 */
class Screen
{
public:
    /** @p slots binds the per-slot Shown state so a departing player's entry does not leak into
     *  whoever takes the slot next. A @ref CustomUi::Panel failure is logged once here, leaving an
     *  empty panel that fails every later @ref Show the same way. */
    Screen(CustomUi& ui, SlotEvents& slots, std::string_view layout, std::string_view rootId,
           int viewer = UiPanel::Everyone);

    /** Make the layout exist for @p slot and unhide its root; @p capture also gives that slot the
     *  cursor. False when it cannot be shown - the panel already logged why. */
    bool Show(int slot, bool capture = false);

    /** Hide the root for @p slot. Keeps the entity and its other state. */
    void Hide(int slot);

    [[nodiscard]] bool Shown(int slot) const;

    UiPanel& Panel();

    /** Forwards to @ref UiPanel::Button. */
    Event<int>& Button(std::string_view id);

    [[nodiscard]] std::string_view Layout() const noexcept;

private:
    UiPanel _panel;
    std::string _root;
    PerSlot<bool> _shown;
};

}  // namespace VoltMod
