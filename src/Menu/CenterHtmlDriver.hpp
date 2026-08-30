#pragma once

#include "Menu/MenuDriver.hpp"

#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>

namespace VoltMod
{

/**
 * @brief WASD-navigated center-HTML menus, the driver every client can show.
 *
 * No workshop addon to publish and no capability behind it, so it works wherever the server does.
 * Re-sends the HTML every frame, which is what center HTML needs to stay on screen.
 *
 * Keeps no per-player state of its own: the cursor and the key debounce live in the manager's
 * session, shared with the Panorama driver, and this driver's page is only ever the cursor's.
 *
 * @ref PanoramaDriver is the alternative - a real clickable panel, at the cost of shipping a
 * layout to clients and two signature-bound capabilities.
 */
class CenterHtmlDriver final : public MenuDriver
{
public:
    CenterHtmlDriver(MenuManager& menus, const MenuServices& services);

    void Present(int slot) override;
    void Dismiss(int slot) override;
    bool HandleInput(int slot) override;

    [[nodiscard]] int RowsPerPage() const override { return ItemsPerPage; }

    // Reset and ShowPage keep the base's empty bodies: this driver holds nothing per menu, and the
    // page it draws is the cursor's own.
};

}  // namespace VoltMod
