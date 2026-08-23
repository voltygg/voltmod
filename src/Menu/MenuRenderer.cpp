#include "Menu/MenuRenderer.hpp"

#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Menu/MenuOption.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <algorithm>
#include <sstream>

namespace CS2Kit::Menu
{

namespace Theme
{
constexpr const char* Gold = "#FFD700";
constexpr const char* Amber = "#FF8C00";
constexpr const char* WarmWhite = "#CCBBAA";
constexpr const char* WarmGray = "#887755";
constexpr const char* Disabled = "#665544";
constexpr const char* NavGold = "#AA8833";
constexpr const char* NavClose = "#AA4422";
constexpr const char* NavBack = "#AA8833";
}  // namespace Theme

// Localized footer label; Get() returns the key unchanged when missing, so fall back to the
// English literal - lets consumers that don't ship nav.* keys still render cleanly.
static std::string FooterLabel(const char* key, const char* fallback, int slot)
{
    auto value = CS2Kit::Detail::Rt().Translations.Get(key, slot);
    return value == key ? std::string(fallback) : value;
}

std::string DefaultHeader(const std::string& title, int currentPage, int totalPages)
{
    std::ostringstream html;
    html << "<font color='" << Theme::Gold << "'><b>" << title << "</b></font>";

    if (totalPages > 1)
    {
        html << " <font class='fontSize-s' color='" << Theme::WarmGray << "'>(" << (currentPage + 1) << "/"
             << totalPages << ")</font>";
    }

    html << "<br>";
    return html.str();
}

static std::string FooterChunk(const char* keyColor, const char* keyText, const std::string& label)
{
    std::ostringstream html;
    html << "<font color='" << keyColor << "'>" << keyText << "</font> "
         << "<font color='" << Theme::WarmGray << "'>" << label << "</font>";
    return html.str();
}

std::string DefaultFooter(bool isSubmenu, bool isPaginated, bool usesHorizontal, int slot)
{
    const char* closeColor = isSubmenu ? Theme::NavBack : Theme::NavClose;
    std::string closeLabel =
        isSubmenu ? FooterLabel("nav.back", "Back", slot) : FooterLabel("nav.close", "Close", slot);

    // First row: W/S, the A/D hint for the current row (value-change or paging), and E.
    std::ostringstream row1;
    row1 << FooterChunk(Theme::NavGold, "[W/S]", FooterLabel("nav.navigate", "Navigate", slot));

    bool hasHorizontalHint = usesHorizontal || isPaginated;
    if (usesHorizontal)
        row1 << " · " << FooterChunk(Theme::NavGold, "[A/D]", FooterLabel("nav.change", "Change", slot));
    else if (isPaginated)
        row1 << " · " << FooterChunk(Theme::NavGold, "[A/D]", FooterLabel("nav.page", "Page", slot));

    const char* selectKey = usesHorizontal ? "nav.confirm" : "nav.select";
    const char* selectFallback = usesHorizontal ? "Confirm" : "Select";
    row1 << " · " << FooterChunk(Theme::Gold, "[E]", FooterLabel(selectKey, selectFallback, slot));

    std::string closeChunk = FooterChunk(closeColor, "[R]", closeLabel);

    std::ostringstream html;
    html << "<font class='fontSize-s'>" << row1.str();

    // With an A/D hint there are four chunks - splitting onto two short rows is more reliable
    // than relying on the HUD's word wrap, which sometimes pushes [R] past the visible area.
    if (hasHorizontalHint)
        html << "<br>" << closeChunk;
    else
        html << " · " << closeChunk;

    html << "</font>";
    return html.str();
}

static std::string RenderItems(const MenuView* menu, int slot, int selectedIndex, int pageStart, int pageEnd)
{
    std::ostringstream html;

    for (int i = pageStart; i < pageEnd; ++i)
    {
        const auto& opt = menu->Items[i];
        if (!opt)
            continue;

        std::string title = opt->GetLabel(slot);
        bool selectable = opt->IsSelectable();
        bool enabled = opt->IsEnabled();

        if (!enabled)
        {
            html << "<font color='" << Theme::Disabled << "'>- " << title << "</font><br>";
        }
        else if (!selectable)
        {
            // Rendered without a cursor glyph - the row is informational, not a target.
            html << "<font color='" << Theme::WarmGray << "'>" << title << "</font><br>";
        }
        else if (i == selectedIndex)
        {
            // No per-row [E]: the cursor signals selection, the footer carries the hint, and a
            // shorter line avoids wrapping in long locales.
            html << "<font color='" << Theme::Amber << "'><b>&gt; " << title << "</b></font><br>";
        }
        else
        {
            html << "<font color='" << Theme::WarmWhite << "'>  " << title << "</font><br>";
        }
    }

    return html.str();
}

std::string RenderMenuHtml(const MenuView* menu, int slot, int selectedIndex, bool isSubmenu)
{
    if (!menu)
    {
        return "";
    }

    int itemCount = static_cast<int>(menu->Items.size());
    int totalPages = itemCount == 0 ? 1 : (itemCount + ItemsPerPage - 1) / ItemsPerPage;
    int currentPage = itemCount == 0 ? 0 : selectedIndex / ItemsPerPage;
    int pageStart = currentPage * ItemsPerPage;
    int pageEnd = std::min(itemCount, pageStart + ItemsPerPage);

    std::ostringstream html;

    if (menu->Layout.Header)
    {
        html << menu->Layout.Header();
    }
    else
    {
        html << DefaultHeader(menu->Title, currentPage, totalPages);
    }

    html << RenderItems(menu, slot, selectedIndex, pageStart, pageEnd);

    if (menu->Layout.Footer)
    {
        html << menu->Layout.Footer();
    }
    else
    {
        bool usesHorizontal = selectedIndex >= 0 && selectedIndex < itemCount && menu->Items[selectedIndex] &&
                              menu->Items[selectedIndex]->IsEnabled() && menu->Items[selectedIndex]->UsesHorizontal();
        html << DefaultFooter(isSubmenu, totalPages > 1, usesHorizontal, slot);
    }

    return html.str();
}

std::string RenderCaptureOverlay(const std::string& menuTitle, std::string_view prompt)
{
    std::ostringstream html;
    html << "<font color='" << Theme::Gold << "'><b>" << menuTitle << "</b></font><br>"
         << "<font color='" << Theme::WarmWhite << "'>" << prompt << "</font><br>"
         << "<font class='fontSize-s' color='" << Theme::WarmGray << "'>Type your answer in chat</font><br>"
         << "<font class='fontSize-s'>"
         << "<font color='" << Theme::NavClose << "'>[R]</font> "
         << "<font color='" << Theme::WarmGray << "'>Cancel</font>"
         << "</font>";
    return html.str();
}

}  // namespace CS2Kit::Menu
