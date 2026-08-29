#pragma once

#include "Menu/MenuDriver.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/Subscriptions.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief The clickable driver: the same menus, drawn into a `custom_hud_layout` the player clicks.
 *
 * Rows carry their kind as a CSS class, so their look is the stylesheet's. Players get a cursor
 * for the session. Needs the layout on the client and the two UI capabilities, which
 * @ref MenuManager::UsePanorama checks before building this.
 *
 * @see @ref custom_ui_guide for the id contract a replacement layout has to honour.
 */
class PanoramaDriver final : public MenuDriver
{
public:
    /** @p panel is the layout to drive, already validated by @ref CustomUi::Panel. Nothing is
     *  spawned until the first @ref Present. */
    PanoramaDriver(MenuManager& menus, const MenuServices& services, UiPanel panel);
    ~PanoramaDriver() override;

    /** Rows one page shows. The layout has to declare exactly this many `vm_row{i}` runs. */
    static constexpr int RowsPerPage = 8;

    void Present(int slot) override;
    void Dismiss(int slot) override;
    void Reset(int slot) override;
    /** Always false: presses arrive as clicks, not as button state read per frame. */
    bool HandleInput(int slot) override;

private:
    /** Panel ids written to. A layout that omits one loses only that piece. */
    static constexpr std::string_view RootId = "vm_root";
    static constexpr std::string_view SubtitleId = "vm_subtitle";
    static constexpr std::string_view PagerId = "vm_pager";
    static constexpr std::string_view PrevId = "vm_prev";
    static constexpr std::string_view NextId = "vm_next";
    static constexpr std::string_view BackId = "vm_back";
    static constexpr std::string_view PromptId = "vm_prompt";

    /** Dialog variables, all on @ref RootId: a `Label` resolves `{s:name}` through its ancestors,
     *  so the labels need no ids. Writing per label id does not work. */
    static constexpr std::string_view TitleVar = "vm_title";
    static constexpr std::string_view SubtitleVar = "vm_subtitle";
    static constexpr std::string_view PageVar = "vm_page";
    static constexpr std::string_view PromptVar = "vm_prompt_text";

    /** Every id and variable name one row needs, built once so a frame allocates nothing. */
    struct RowIds
    {
        std::string Row;    ///< row panel id: classes
        std::string Label;  ///< dialog variable on the root panel
        std::string Value;  ///< dialog variable on the root panel
    };

    /** The class a row of @p kind carries. Every kind is written on every row, so a row that
     *  changes kind does not keep the old one underneath. */
    static std::string_view ClassFor(MenuRowKind kind);

    /** Write row @p row of the current page for @p slot from @p item, and show it. */
    void DrawRow(int slot, int row, const MenuItem& item);

    /** Hide rows @p row and after, so a short page leaves no stale text on screen. */
    void HideRowsFrom(int slot, int row);

    /** Subscribe to presses. Deferred to the first draw, not taken in the constructor:
     *  subscribing is what installs the click hook, and a menu nobody has opened should not
     *  arm one. */
    void BindClicks();

    void OnClick(const UiClick& click);
    void TurnPage(int slot, int delta);

    /** The item index row @p row of @p slot's current page stands for. */
    [[nodiscard]] int ItemIndex(int slot, int row) const;

    UiPanel _panel;
    std::vector<RowIds> _rows;
    /** Which page of a long menu each player is on. A click driver has no cursor to derive one
     *  from, so it keeps the page itself. */
    PerSlot<int> _pages;
    /** Declared after everything their handlers touch. */
    Subscriptions _subs;
    /** The one press handler, taken on the first draw. */
    Subscription _clicks;
};

}  // namespace VoltMod
