#pragma once

#include "Menu/MenuDriver.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <cstdint>
#include <vector>

namespace VoltMod
{

/**
 * @brief WASD-navigated center-HTML menus, the driver every client can show.
 *
 * No workshop addon to publish and no capability behind it, so it works wherever the server does.
 * Reads button state each frame and re-sends the HTML, which is what center HTML needs to stay on
 * screen; the cursor is this driver's alone, because nothing else has one.
 *
 * @ref PanoramaDriver is the alternative - a real clickable panel, at the cost of shipping a
 * layout to clients and a Windows-only capability.
 */
class CenterHtmlDriver final : public MenuDriver
{
public:
    CenterHtmlDriver(MenuManager& menus, const MenuServices& services);

    void Present(int slot) override;
    void Dismiss(int slot) override;
    void Reset(int slot) override;
    bool HandleInput(int slot) override;

private:
    /** What this driver tracks per player: where the cursor is, and what the last frame's input
     *  was, so a held key does not scroll the menu away. */
    struct PlayerState
    {
        int SelectedIndex = 0;
        int64_t LastInputTime = 0;
        uint64_t PrevButtons = 0;
    };

    /** Jump @p idx by @p pageDelta pages, keeping the in-page offset. Paging by cursor index is
     *  this driver's alone: only a keyboard has a cursor to derive a page from. */
    static void JumpPage(int slot, const std::vector<MenuItem>& items, int& idx, int pageDelta);

    /** Act on the keys pressed this frame. True when one of them was consumed. */
    bool HandlePressed(int slot, const Menu& menu, uint64_t pressed);

    static constexpr int64_t InputDebounceMs = 200;

    PerSlot<PlayerState> _states;
};

}  // namespace VoltMod
