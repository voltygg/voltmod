#include "Menu/Html/MenuRenderer.hpp"

#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/MenuOption.hpp>
#include <algorithm>
#include <sstream>
#include <string>
#include <string_view>

namespace VoltMod
{

namespace Theme
{
constexpr std::string_view Gold = "#FFD700";
constexpr std::string_view Amber = "#FF8C00";
constexpr std::string_view WarmWhite = "#CCBBAA";
constexpr std::string_view WarmGray = "#887755";
constexpr std::string_view Disabled = "#665544";
constexpr std::string_view NavGold = "#AA8833";
constexpr std::string_view NavClose = "#AA4422";
constexpr std::string_view NavBack = "#AA8833";
}  // namespace Theme

// Localized footer label; Get() returns the key unchanged when missing, so fall back to the
// English literal - lets consumers that don't ship nav.* keys still render cleanly.
static std::string FooterLabel(Translations& translations, std::string_view key, std::string_view fallback, int slot)
{
    auto value = translations.Get(std::string(key), slot);
    return value == key ? std::string(fallback) : value;
}

std::string DefaultHeader(const std::string& title, const std::string& subtitle, int currentPage, int totalPages)
{
    std::ostringstream html;
    // Titles routinely interpolate a player name, so the one place they become markup escapes them.
    html << "<font color='" << Theme::Gold << "'><b>" << Strings::EscapeHtml(title) << "</b></font>";

    if (!subtitle.empty())
    {
        html << " <font class='fontSize-s' color='" << Theme::WarmGray << "'>" << Strings::EscapeHtml(subtitle)
             << "</font>";
    }

    if (totalPages > 1)
    {
        html << " <font class='fontSize-s' color='" << Theme::WarmGray << "'>(" << (currentPage + 1) << "/"
             << totalPages << ")</font>";
    }

    html << "<br>";
    return html.str();
}

static std::string FooterChunk(std::string_view keyColor, std::string_view keyText, const std::string& label)
{
    std::ostringstream html;
    html << "<font color='" << keyColor << "'>" << keyText << "</font> "
         << "<font color='" << Theme::WarmGray << "'>" << label << "</font>";
    return html.str();
}

std::string DefaultFooter(bool isSubmenu, bool isPaginated, bool usesHorizontal, int slot, Translations& translations)
{
    auto label = [&](std::string_view key, std::string_view fallback) {
        return FooterLabel(translations, key, fallback, slot);
    };

    const std::string_view closeColor = isSubmenu ? Theme::NavBack : Theme::NavClose;
    std::string closeLabel = isSubmenu ? label("nav.back", "Back") : label("nav.close", "Close");

    // First row: W/S, the A/D hint for the current row (value-change or paging), and E.
    std::ostringstream row1;
    row1 << FooterChunk(Theme::NavGold, "[W/S]", label("nav.navigate", "Navigate"));

    bool hasHorizontalHint = usesHorizontal || isPaginated;
    if (usesHorizontal)
        row1 << " · " << FooterChunk(Theme::NavGold, "[A/D]", label("nav.change", "Change"));
    else if (isPaginated)
        row1 << " · " << FooterChunk(Theme::NavGold, "[A/D]", label("nav.page", "Page"));

    const std::string_view selectKey = usesHorizontal ? "nav.confirm" : "nav.select";
    const std::string_view selectFallback = usesHorizontal ? "Confirm" : "Select";
    row1 << " · " << FooterChunk(Theme::Gold, "[E]", label(selectKey, selectFallback));

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

// One row as this renderer spells it: "Title", "Title: ON", "Title: &lt; 100% &gt;". The model
// carries the two halves as plain text, so the decoration and the escaping are both decided here.
static std::string RowText(const MenuOption& option, int slot)
{
    MenuRow row = option.Describe(slot);
    std::string text = Strings::EscapeHtml(row.Label);
    if (row.Value.empty())
        return text;

    text += ": ";

    // The arrows say "A and D change this"; nothing else in a line of center HTML does.
    if (row.Kind == MenuRowKind::Choice)
        return text + "&lt; " + Strings::EscapeHtml(row.Value) + " &gt;";

    return text + Strings::EscapeHtml(row.Value);
}

static std::string RenderItems(const MenuView* menu, int slot, int selectedIndex, int pageStart, int pageEnd)
{
    std::ostringstream html;

    for (int i = pageStart; i < pageEnd; ++i)
    {
        const auto& opt = menu->Items[i];
        if (!opt)
            continue;

        std::string title = RowText(*opt, slot);
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

std::string RenderMenuHtml(const MenuView* menu, int slot, int selectedIndex, bool isSubmenu,
                           Translations& translations)
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

    html << DefaultHeader(menu->Title, menu->Subtitle, currentPage, totalPages);
    html << RenderItems(menu, slot, selectedIndex, pageStart, pageEnd);

    const bool usesHorizontal = selectedIndex >= 0 && selectedIndex < itemCount && menu->Items[selectedIndex] &&
                                menu->Items[selectedIndex]->IsEnabled() && menu->Items[selectedIndex]->UsesHorizontal();
    html << DefaultFooter(isSubmenu, totalPages > 1, usesHorizontal, slot, translations);

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

}  // namespace VoltMod
