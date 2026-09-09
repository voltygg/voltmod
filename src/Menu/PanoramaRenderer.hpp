#pragma once

#include "Menu/MenuCore.hpp"
#include "Menu/MenuLayout.hpp"
#include "Menu/MenuRenderer.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @brief Menus drawn as a clickable Panorama panel, private to the player so it survives death
 * and spectating.
 *
 * Needs the menu layout on the client and a cursor to press it with, so a player who has neither
 * is drawn to by another surface instead.
 */
class PanoramaRenderer final : public MenuRenderer
{
public:
    /** All three must outlive the renderer; @ref MenuManager owns them. @p session is what Back
     *  and Close go through, so a press closes the session the same way a key does. */
    PanoramaRenderer(const MenuServices& services, MenuCore& core, MenuSession& session,
                     const PanoramaMenuOptions& options);
    ~PanoramaRenderer() override;

    bool Attach(int slot) override;
    bool Present(int slot) override;
    void Dismiss(int slot) override;
    [[nodiscard]] int RowsPerPage() const override { return static_cast<int>(_rows.size()); }
    void ShowPage(int slot, int page) override;

private:
    /** @p slot's own panel, made on first use; empty (and logged) when it could not be. */
    UiPanel& PanelFor(int slot);

    /** Deferred to the first draw: subscribing installs the click hook, and a menu nobody has
     *  opened should not arm one. */
    void BindClicks();

    void OnClick(const UiClick& click);
    void OnNavClick(int slot, int tab);
    void TurnPage(int slot, int delta);

    void DrawRow(UiPanel& panel, int slot, int row, int index);
    void WriteRow(UiPanel& panel, int slot, int row, const MenuRow& described, bool selected);
    void DrawEmpty(UiPanel& panel, int slot);
    void HideRowsFrom(UiPanel& panel, int slot, int row);
    void DrawNav(UiPanel& panel, int slot);

    /** Item indexes of the root menu's submenu rows, in nav-button order. */
    [[nodiscard]] std::vector<int> NavRows(int slot);

    [[nodiscard]] int ItemIndex(int slot, int row) const { return _pages[slot] * RowsPerPage() + row; }

    MenuServices _services;
    MenuCore& _core;
    MenuSession& _session;
    MenuLayoutSpec _spec;
    std::vector<MenuRowIds> _rows;
    std::vector<std::string> _navIds;
    std::vector<std::string> _navVars;
    /** One private panel per player being drawn to, dropped by @ref Dismiss and with the slot. */
    PerSlot<UiPanel> _panels;
    PerSlot<int> _pages;
    /** Declared last: click delivery drops before the state it touches. */
    Subscription _clicks;
};

}  // namespace VoltMod
