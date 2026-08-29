#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <functional>
#include <string>
#include <string_view>

namespace VoltMod
{

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

/** What a menu looks like to the renderer beyond its rows: whose screen it is, where the cursor
 *  sits, and how a row describes itself right now. */
struct CenterHtmlView
{
    /** Row @p index as @ref MenuManager::Describe fills it in, so @ref MenuRow::Pending and
     *  @ref MenuRow::Changed are already answered. */
    std::function<MenuRow(int index)> Describe;
    /** The titles under this menu, joined; empty at the root. */
    std::string Breadcrumb;
    int Slot = 0;
    int SelectedIndex = 0;
    bool IsSubmenu = false;
};

/** Renders the HTML for a menu, including its items and layout. @p translations localizes the
 *  default footer's nav labels and the empty-menu line, each looked up only where it is drawn. */
std::string RenderMenuHtml(const Menu* menu, const CenterHtmlView& view, Translations& translations);

/** Renders the chat-input capture overlay shown while a player is typing a value. */
std::string RenderCaptureOverlay(const std::string& menuTitle, std::string_view prompt);

/** Generates the default header HTML for a menu. Empty parts of @p header add nothing. */
std::string DefaultHeader(const CenterHtmlHeader& header);

/**
 * Generates the default footer HTML for a menu.
 * @param isSubmenu True if this menu is a submenu (shows "Back" hint), false if it's a root menu (shows "Close" hint).
 * @param isPaginated True if the menu has multiple pages of items (shows page navigation hints)
 * @param usesHorizontal True if the selected row's A/D edits its value (shows "Change"/"Confirm" hints).
 * @param slot Player slot used to look up the nav-label translations.
 * @param translations Table the nav labels are looked up in.
 * @return The generated HTML string for the menu footer.
 */
std::string DefaultFooter(bool isSubmenu, bool isPaginated, bool usesHorizontal, int slot, Translations& translations);

}  // namespace VoltMod
