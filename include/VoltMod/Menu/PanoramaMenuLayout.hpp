#pragma once

#include <VoltMod/Menu/MenuModel.hpp>
#include <VoltMod/Ui/PlayerScreens.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** What a pressed button on the menu stands for. */
enum class MenuButtonKind
{
    Cancel,
    Back,
    Close,
    PreviousPage,
    NextPage,
    Tab,
    Row,
    StepDown,
    StepUp,
};

struct MenuButton
{
    MenuButtonKind Kind;
    /** The tab or row index, for the kinds that have one. */
    int Index = 0;
};

/** The text above the rows. An empty subtitle hides its line. */
struct MenuHeader
{
    std::string_view Brand;
    std::string_view BrandSubtitle;
    std::string_view Breadcrumb;
    std::string_view Title;
    std::string_view Subtitle;
};

/** One sidebar tab as drawn. */
struct MenuTab
{
    std::string_view Label;
    std::string_view Icon;
    bool Selected = false;
};

/**
 * @brief The screen built from the `menu` Panorama block, as @ref PanoramaMenu draws on it.
 *
 * Knows element ids and classes and nothing about menus; every call names the player it draws
 * for. A null tab or row hides it, and so does empty text for the empty-list line, the pager and
 * the prompt. @p screen is the layout name; @p tabs, @p rows and @p iconNames come from its
 * generated header (`Tabs.size()`, `Rows.size()`, `IconSetNames`). Each player gets their own
 * screen, so the menu survives death and spectating. @p screens and @p iconNames must outlive
 * this; the generated header's array does.
 */
class PanoramaMenuLayout
{
public:
    PanoramaMenuLayout(ScreenManager& screens, std::string_view screen, std::size_t tabs, std::size_t rows,
                       std::span<const std::string_view> iconNames);

    /** Rows on one page. */
    int RowCount() const { return static_cast<int>(_rows.size()); }

    /** Sidebar tabs, filled from the root menu's submenus. */
    int TabCount() const { return static_cast<int>(_tabs.size()); }

    /** Spawn if needed, unhide, and give the cursor. False when @p slot cannot be drawn to. */
    bool Show(int slot);
    void Hide(int slot);

    void SetHeader(int slot, const MenuHeader& header);
    void SetSidebarVisible(int slot, bool visible);
    /** Show the home markup in the rows' place. A screen without any does nothing. */
    void SetHomeVisible(int slot, bool visible);
    void SetTab(int slot, int index, const MenuTab* tab);
    void SetRow(int slot, int index, const MenuRow* row, std::string_view pendingHint);
    void SetEmpty(int slot, std::string_view text);
    void SetPager(int slot, std::string_view text);
    void SetPrompt(int slot, std::string_view text, std::string_view hint);
    /** An empty @p back hides the back button: the root has nothing to go back to. */
    void SetFooter(int slot, std::string_view back, std::string_view cancel);

    /** The button a pressed id names, or nothing for an id outside this screen. */
    std::optional<MenuButton> ButtonFor(std::string_view id) const;

    /** Fills `{s:<variable>}` in markup the screen adds itself, such as its `home` panel. Written
     *  with every header, so it outlives a respawned screen. */
    void AddText(std::string_view variable, std::function<std::string(int slot)> text);

private:
    struct TabIds
    {
        std::string Id;
        std::string Icon;
        std::string LabelVar;
    };

    struct RowIds
    {
        std::string Id;
        std::string Button;
        std::string Decrease;
        std::string Increase;
        std::string LabelVar;
        std::string HintVar;
        std::string ValueVar;
    };

    struct ScreenText
    {
        std::string Variable;
        std::function<std::string(int slot)> Value;
    };

    PlayerScreens _screens;
    std::string _root;
    std::string _subtitle;
    std::string _close;
    std::string _empty;
    std::string _prompt;
    std::string _cancel;
    std::string _back;
    std::string _page;
    std::string _pagePrevious;
    std::string _pageNext;
    std::vector<TabIds> _tabs;
    std::vector<RowIds> _rows;
    std::span<const std::string_view> _iconNames;
    std::vector<ScreenText> _texts;
};

}  // namespace VoltMod
