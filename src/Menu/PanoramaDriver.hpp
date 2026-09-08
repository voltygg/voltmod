#pragma once

#include "Menu/MenuDriver.hpp"

#include <VoltMod/Core/PerSlot.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Core/SubscriptionScope.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** Renders clickable Panorama menus. @see @ref custom_ui_guide for the layout contract. */
class PanoramaDriver final : public MenuDriver
{
public:
    PanoramaDriver(ActiveMenus& menus, MenuSession& session, const MenuServices& services, UiPanel panel);
    ~PanoramaDriver() override;

    /** Rows one page shows. The layout has to declare exactly this many `vm_row{i}` runs. */
    static constexpr int RowsPerPageCount = 8;

    void Present(int slot) override;
    void Dismiss(int slot) override;
    void Reset(int slot) override;
    bool HandleInput(int slot) override;

    [[nodiscard]] int RowsPerPage() const override { return RowsPerPageCount; }

    void ShowPage(int slot, int page) override;

private:
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
    static constexpr std::string_view BreadcrumbVar = "vm_breadcrumb";
    static constexpr std::string_view PageVar = "vm_page";
    static constexpr std::string_view PromptVar = "vm_prompt_text";
    static constexpr std::string_view PromptHintVar = "vm_prompt_hint";

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

    static std::string_view ClassFor(MenuRowKind kind);

    void DrawRow(int slot, int row, int index);

    /** Write @p described into row @p row for @p slot, drawing it selected when @p selected.
     *  Every class the vocabulary has is written here, whether or not this row carries it. */
    void WriteRow(int slot, int row, const MenuRow& described, bool selected);

    void DrawEmpty(int slot);

    void HideRowsFrom(int slot, int row);

    /** Subscribe to presses. Deferred to the first draw, not taken in the constructor:
     *  subscribing is what installs the click hook, and a menu nobody has opened should not
     *  arm one. */
    void BindClicks();

    void OnClick(const UiClick& click);
    void TurnPage(int slot, int delta);

    [[nodiscard]] int ItemIndex(int slot, int row) const;

    UiPanel _panel;
    std::vector<RowIds> _rows;
    PerSlot<int> _pages;
    SubscriptionScope _subs;
    Subscription _clicks;
};

}  // namespace VoltMod
