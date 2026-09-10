#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Rows one center-HTML page holds. A renderer limit, not a menu-model limit. */
inline constexpr int CenterHtmlRowsPerPage = 5;

/** The header block: what the menu is called, where it sits, and which page is showing. */
struct CenterHtmlHeader
{
    std::string_view Title;
    std::string_view Subtitle;
    /** The menus this one was reached through, already joined; drawn small before the title. */
    std::string_view Breadcrumb;
    int Page = 0;
    int Pages = 1;
};

/** Menu state needed by the renderer. */
struct CenterHtmlView
{
    /** Row @p index as @ref MenuStack::Describe fills it in, so @ref MenuRow::Pending and
     *  @ref MenuRow::Changed are already answered. */
    std::function<MenuRow(int index)> Describe;
    /** The titles under this menu, joined; empty at the root. Borrowed for the render call. */
    std::string_view Breadcrumb;
    int Slot = 0;
    int SelectedIndex = 0;
    bool IsSubmenu = false;
};

/** Renders the HTML for a menu, including its items and layout. @p translations localizes the
 *  default footer's nav labels and the empty-menu line, each looked up only where it is drawn. */
std::string RenderMenuHtml(const Menu* menu, const CenterHtmlView& view, Translations& translations);

/** Renders the chat-input capture overlay shown while a player is typing a value. */
std::string RenderCaptureOverlay(const std::string& menuTitle, std::string_view prompt);

/** The header HTML for a menu. Empty parts of @p header add nothing. */
std::string RenderHeader(const CenterHtmlHeader& header);

/**
 * The footer HTML for a menu: the key hints.
 * @param isSubmenu True if this menu is a submenu (shows "Back" hint), false if it's a root menu (shows "Close" hint).
 * @param isPaginated True if the menu has multiple pages of items (shows page navigation hints)
 * @param selectedRowSteps True when A/D steps the selected row's value rather than paging (shows "Change"/"Confirm" hints).
 * @param slot Player slot used to look up the nav-label translations.
 * @param translations Table the nav labels are looked up in.
 * @return The generated HTML string for the menu footer.
 */
std::string RenderFooter(bool isSubmenu, bool isPaginated, bool selectedRowSteps, int slot, Translations& translations);

}  // namespace VoltMod
