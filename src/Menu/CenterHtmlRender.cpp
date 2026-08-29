#include "Menu/CenterHtmlRender.hpp"

#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
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

std::string DefaultHeader(const CenterHtmlHeader& header)
{
    std::ostringstream html;

    // The path taken to get here, ahead of the title and dimmer than it, so a submenu says where
    // it sits without the title having to repeat it.
    if (!header.Crumbs.empty())
    {
        html << "<font class='fontSize-s' color='" << Theme::WarmGray << "'>" << Strings::EscapeHtml(header.Crumbs)
             << " › </font>";
    }

    // Titles routinely interpolate a player name, so the one place they become markup escapes them.
    html << "<font color='" << Theme::Gold << "'><b>" << Strings::EscapeHtml(header.Title) << "</b></font>";

    if (!header.Subtitle.empty())
    {
        html << " <font class='fontSize-s' color='" << Theme::WarmGray << "'>" << Strings::EscapeHtml(header.Subtitle)
             << "</font>";
    }

    if (header.Pages > 1)
    {
        html << " <font class='fontSize-s' color='" << Theme::WarmGray << "'>(" << (header.Page + 1) << "/"
             << header.Pages << ")</font>";
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
    // Fall back to the English literal so consumers that don't ship nav.* keys still render cleanly.
    auto label = [&](std::string_view key, std::string_view fallback) {
        return translations.GetOr(key, slot, fallback);
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

// One row as this renderer spells it: "Title", "Title: [ON]", "Title: &lt; 100 HP… &gt; *". The
// model carries the two halves as plain text, so the decoration and the escaping are decided here.
// The two markers mirror what the Panorama driver draws as classes: `…` while a stepped value is
// still waiting to be applied, `*` for a moment after a value moved.
static std::string RowText(const MenuRow& row)
{
    std::string text = Strings::EscapeHtml(row.Label);
    if (row.Value.empty())
        return text;

    std::string value = Strings::EscapeHtml(row.Value);
    if (row.Pending)
        value += "…";

    // The brackets say "this is a switch" and the arrows "A and D change this"; nothing else in a
    // line of center HTML does.
    if (row.Kind == MenuRowKind::Toggle)
        value = "[" + value + "]";
    else if (row.Kind == MenuRowKind::Choice)
        value = "&lt; " + value + " &gt;";

    text += ": " + value;
    if (row.Changed)
        text += " *";
    return text;
}

// @p selected takes the cursor's row as it was described for the page, so the footer that needs it
// does not describe the same row a second time.
static std::string RenderItems(const CenterHtmlView& view, int pageStart, int pageEnd, MenuRow& selected)
{
    std::ostringstream html;

    for (int i = pageStart; i < pageEnd; ++i)
    {
        const MenuRow row = view.Describe(i);
        if (i == view.SelectedIndex)
            selected = row;
        std::string title = RowText(row);
        bool selectable = row.Selectable;
        bool enabled = row.Enabled;

        if (!enabled)
        {
            html << "<font color='" << Theme::Disabled << "'>- " << title << "</font><br>";
        }
        else if (!selectable)
        {
            // Rendered without a cursor glyph - the row is informational, not a target.
            html << "<font color='" << Theme::WarmGray << "'>" << title << "</font><br>";
        }
        else if (i == view.SelectedIndex)
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

std::string RenderMenuHtml(const Menu* menu, const CenterHtmlView& view, Translations& translations)
{
    if (!menu || !view.Describe)
    {
        return "";
    }

    int itemCount = static_cast<int>(menu->Items.size());
    int totalPages = PageCount(itemCount, ItemsPerPage);
    int currentPage = itemCount == 0 ? 0 : view.SelectedIndex / ItemsPerPage;
    int pageStart = currentPage * ItemsPerPage;
    int pageEnd = std::min(itemCount, pageStart + ItemsPerPage);

    std::ostringstream html;

    html << DefaultHeader({.Title = menu->Title,
                           .Subtitle = menu->Subtitle,
                           .Crumbs = view.Crumbs,
                           .Page = currentPage,
                           .Pages = totalPages});

    // Only the branch that draws it pays for it: the empty line is looked up here rather than by
    // every caller on every frame, and the cursor's row comes back from the page that drew it.
    MenuRow selected{.Enabled = false};
    if (itemCount == 0)
    {
        const std::string empty = translations.GetOr("menu.empty", view.Slot, "Nothing here");
        html << "<font color='" << Theme::WarmGray << "'>" << Strings::EscapeHtml(empty) << "</font><br>";
    }
    else
    {
        html << RenderItems(view, pageStart, pageEnd, selected);
    }

    html << DefaultFooter(view.IsSubmenu, totalPages > 1, selected.Enabled && selected.Steppable, view.Slot,
                          translations);

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
