#pragma once

#include <VoltMod/Menu/Menu.hpp>
#include <optional>
#include <string_view>

namespace VoltMod
{

/** What a pressed button on a menu layout stands for. */
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
 * @brief A plugin's Panorama menu layout, as @ref PanoramaMenu draws on it.
 *
 * Knows element ids and classes and nothing about menus; every call names the player it draws
 * for. A null tab or row hides it, and so does empty text for the empty-list line, the pager and
 * the prompt. SDK-free.
 */
class MenuLayout
{
public:
    virtual ~MenuLayout() = default;

    MenuLayout(const MenuLayout&) = delete;
    MenuLayout& operator=(const MenuLayout&) = delete;

    /** Rows on one page. */
    virtual int RowCount() const = 0;

    /** Sidebar tabs, filled from the root menu's submenus. Zero for a layout without a sidebar. */
    virtual int TabCount() const = 0;

    /** Spawn if needed, unhide, and give the cursor. False when @p slot cannot be drawn to. */
    virtual bool Show(int slot) = 0;
    virtual void Hide(int slot) = 0;

    virtual void SetHeader(int slot, const MenuHeader& header) = 0;
    virtual void SetSidebarVisible(int slot, bool visible) = 0;
    /** Show the home markup in the rows' place. A layout without any does nothing. */
    virtual void SetHomeVisible(int slot, bool visible) = 0;
    virtual void SetTab(int slot, int index, const MenuTab* tab) = 0;
    virtual void SetRow(int slot, int index, const MenuRow* row, std::string_view pendingHint) = 0;
    virtual void SetEmpty(int slot, std::string_view text) = 0;
    virtual void SetPager(int slot, std::string_view text) = 0;
    virtual void SetPrompt(int slot, std::string_view text, std::string_view hint) = 0;
    /** An empty @p back hides the back button: the root has nothing to go back to. */
    virtual void SetFooter(int slot, std::string_view back, std::string_view cancel) = 0;

    /** The button a pressed id names, or nothing for an id outside this layout. */
    virtual std::optional<MenuButton> ButtonFor(std::string_view id) const = 0;

protected:
    MenuLayout() = default;
};

}  // namespace VoltMod
