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
 * Rows carry what they are and how they stand as CSS classes, so their look is the stylesheet's.
 * Players get a cursor for the session. Needs the layout on the client and the two UI
 * capabilities, which @ref MenuManager::UsePanorama checks before building this.
 *
 * Keys work here too, and the cursor they move is the session's own, so a player may click one
 * row and step the next one with A/D. Whether the keys arrive at all depends on what input
 * capture leaves the server, so nothing here needs them: no key is simply no change, and
 * @ref MenuOptions::Keyboard turns the reading off for a session that wants clicks only.
 *
 * @see @ref custom_ui_guide for the id and class contract a replacement layout has to honour.
 */
class PanoramaDriver final : public MenuDriver
{
public:
    /** @p panel is the layout to drive, already validated by @ref CustomUi::Panel. Nothing is
     *  spawned until the first @ref Present. */
    PanoramaDriver(MenuManager& menus, const MenuServices& services, UiPanel panel);
    ~PanoramaDriver() override;

    /** Rows one page shows. The layout has to declare exactly this many `vm_row{i}` runs. */
    static constexpr int RowsPerPageCount = 8;

    void Present(int slot) override;
    void Dismiss(int slot) override;
    void Reset(int slot) override;
    /** Reads the shared keys unless this session asked for clicks only. */
    bool HandleInput(int slot) override;

    [[nodiscard]] int RowsPerPage() const override { return RowsPerPageCount; }

    /** The cursor moved onto @p page, so the page this driver keeps follows it. */
    void ShowPage(int slot, int page) override;

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
    static constexpr std::string_view CrumbsVar = "vm_crumbs";
    static constexpr std::string_view PageVar = "vm_page";
    static constexpr std::string_view PromptVar = "vm_prompt_text";
    static constexpr std::string_view PromptHintVar = "vm_prompt_hint";

    /** Every id and variable name one row needs, built once so a frame allocates nothing. */
    struct RowIds
    {
        std::string Row;    ///< row panel id: classes
        std::string Label;  ///< dialog variable on the root panel
        std::string Value;  ///< dialog variable on the root panel
    };

    /**
     * The class vocabulary a stylesheet styles against, grouped so a name here cannot be read as
     * one of the driver's own. Every one of them is written on every draw, on or off, so a row
     * never keeps a class the state it stood for has left - and the panel's write cache makes the
     * ones that did not change free.
     */
    struct Css
    {
        static constexpr std::string_view Hidden = "Hidden";
        static constexpr std::string_view Disabled = "Disabled";
        static constexpr std::string_view Selected = "Selected";
        static constexpr std::string_view Changed = "Changed";
        static constexpr std::string_view Pending = "Pending";
        static constexpr std::string_view HasValue = "HasValue";
        static constexpr std::string_view HasSteppers = "HasSteppers";
        static constexpr std::string_view On = "On";
        static constexpr std::string_view Prompting = "Prompting";
        static constexpr std::string_view KeyHints = "KeyHints";
        static constexpr std::string_view Root = "Root";
    };

    /** The class a row of @p kind carries. Every kind is written on every row, so a row that
     *  changes kind does not keep the old one underneath. */
    static std::string_view ClassFor(MenuRowKind kind);

    /** Write row @p row of the current page for @p slot from item @p index, and show it. */
    void DrawRow(int slot, int row, int index);

    /** Write @p described into row @p row for @p slot, drawing it selected when @p selected.
     *  Every class the vocabulary has is written here, whether or not this row carries it. */
    void WriteRow(int slot, int row, const MenuRow& described, bool selected);

    /** Write the one row an empty menu draws, and hide the rest. */
    void DrawEmpty(int slot);

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
    /** Which page of a long menu each player is on. Clicks turn it directly; the keyboard cursor
     *  drags it along through @ref ShowPage. */
    PerSlot<int> _pages;
    /** Declared after everything their handlers touch. */
    Subscriptions _subs;
    /** The one press handler, taken on the first draw. */
    Subscription _clicks;
};

}  // namespace VoltMod
