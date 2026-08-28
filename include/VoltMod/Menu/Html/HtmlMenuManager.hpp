#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Menu/MenuHost.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief WASD-navigated center-HTML menus for all players.
 *
 * The @ref MenuHost every client can show: no workshop addon to publish and no capability behind
 * it, so it works wherever the server does. Reads button state each tick from a scheduler pump it
 * registers for itself and re-sends the HTML, which is what center HTML needs to stay on screen.
 *
 * @ref UiMenuManager is the Panorama alternative - a real clickable panel, at the cost of shipping
 * a layout to clients and a Windows-only capability.
 */
class HtmlMenuManager final : public MenuHost
{
public:
    /**
     * Takes exactly the services menu dispatch and context rows use. All must outlive the
     * manager. The constructor subscribes to @p scheduler and @p slots, so both must already be
     * constructed - true whenever a Runtime builds this in declaration order.
     */
    HtmlMenuManager(Scheduler& scheduler, SlotEvents& slots, EntitySystem& entities, Messages& messages,
                    ChatInput& chatInput, Translations& translations, Policy& policy, PlayerManager& players);

private:
    /** Per-tick driver: reads buttons, advances selection, and re-renders. */
    void OnGameFrame();

    void HandleInput(int slot, uint64_t buttons, uint64_t prevButtons);

    /** Jump @p idx by @p pageDelta pages, keeping the in-page offset. A member because it
     *  needs the base's cursor rule; paging by cursor index is this driver's alone. */
    static void JumpPage(const std::vector<std::shared_ptr<MenuOption>>& items, int& idx, int pageDelta);

    void Present(int slot) override;
    void Dismiss(int slot) override;

    Messages& _messages;
    static constexpr int64_t InputDebounceMs = 200;
    /** Declared last: the frame pump drops before the state it touches. */
    Subscription _pump;
};

}  // namespace VoltMod
