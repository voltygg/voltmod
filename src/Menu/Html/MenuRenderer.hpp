#pragma once

#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <string>
#include <string_view>

namespace VoltMod
{
/** Renders the HTML for a menu, including its items and layout. @p translations localizes the
 *  default footer's nav labels. */
std::string RenderMenuHtml(const Menu* menu, int slot, int selectedIndex, bool isSubmenu, Translations& translations);

/** Renders the chat-input capture overlay shown while a player is typing a value. */
std::string RenderCaptureOverlay(const std::string& menuTitle, std::string_view prompt);

/** Generates the default header HTML for a menu. @p subtitle is optional and rendered small. */
std::string DefaultHeader(const std::string& title, const std::string& subtitle, int currentPage, int totalPages);

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
