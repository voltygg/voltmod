#include "Menu/CenterHtmlRender.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <doctest/doctest.h>
#include <memory>
#include <string>

using VoltMod::ButtonRow;
using VoltMod::CenterHtmlView;
using VoltMod::ChoiceRow;
using VoltMod::DefaultFooter;
using VoltMod::DefaultHeader;
using VoltMod::ItemsPerPage;
using VoltMod::Menu;
using VoltMod::MenuBuilder;
using VoltMod::MenuRow;
using VoltMod::RenderMenuHtml;
using VoltMod::SlotEvents;
using VoltMod::ToggleRow;
using VoltMod::Translations;

// The manager fills Pending and Changed before a driver draws a row; here the rows describe
// themselves and nothing is pending, which is what an untouched menu looks like.
static CenterHtmlView ViewOf(const Menu& menu, int selectedIndex, bool isSubmenu)
{
    return CenterHtmlView{
        .Describe =
            [&menu](int index) {
                const VoltMod::MenuItem& item = menu.Items[static_cast<std::size_t>(index)];
                return item.Describe ? item.Describe(0) : MenuRow{.Enabled = false, .Selectable = false};
            },
        .EmptyLabel = "Nothing here",
        .SelectedIndex = selectedIndex,
        .IsSubmenu = isSubmenu,
    };
}

TEST_CASE("CenterHtmlRender: DefaultHeader shows the title and hides the page count for one page")
{
    CHECK(DefaultHeader({.Title = "Admin Panel"}).find("Admin Panel") != std::string::npos);
    CHECK(DefaultHeader({.Title = "Admin Panel"}).find("(1/1)") == std::string::npos);
    CHECK(DefaultHeader({.Title = "Admin Panel", .Page = 1, .Pages = 3}).find("(2/3)") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: a subtitle rides next to the title, and an empty one adds nothing")
{
    const std::string bare = DefaultHeader({.Title = "Admin Panel"});
    const std::string withSubtitle = DefaultHeader({.Title = "Admin Panel", .Subtitle = "v1.2.0"});

    CHECK(withSubtitle.find("v1.2.0") != std::string::npos);
    CHECK(bare == DefaultHeader({.Title = "Admin Panel"}));
    CHECK(bare.length() < withSubtitle.length());
}

TEST_CASE("CenterHtmlRender: the breadcrumb draws ahead of the title, and the root has none")
{
    const std::string root = DefaultHeader({.Title = "Punish"});
    const std::string nested = DefaultHeader({.Title = "Punish", .Crumbs = "Admin Panel"});

    CHECK(nested.find("Admin Panel") != std::string::npos);
    CHECK(nested.find("Admin Panel") < nested.find("Punish"));
    CHECK(root.find("Admin Panel") == std::string::npos);
}

TEST_CASE("CenterHtmlRender: RenderMenuHtml marks the selected row and dims disabled rows")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu")
                    .Button("Enabled Row", [](int) {})
                    .Add(ButtonRow{.Label = "Disabled Row", .Enabled = false})
                    .Build();

    std::string html = RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations);
    CHECK(html.find("Test Menu") != std::string::npos);
    CHECK(html.find("&gt; Enabled Row") != std::string::npos);  // cursor marker on the selected row
    CHECK(html.find("Disabled Row") != std::string::npos);
    // The disabled row is dimmed and carries no cursor marker of its own.
    CHECK(html.find("&gt; Disabled Row") == std::string::npos);
}

TEST_CASE("CenterHtmlRender: a non-selectable Text row renders without a cursor")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Text("Just a heading").Button("Pick me", [](int) {}).Build();

    // Selection lands on index 1: MenuManager::Open skips non-selectable rows when it opens a
    // menu, and this render call mirrors that already-adjusted index.
    std::string html = RenderMenuHtml(menu.get(), ViewOf(*menu, 1, false), translations);
    CHECK(html.find("Just a heading") != std::string::npos);
    CHECK(html.find("&gt; Just a heading") == std::string::npos);
    CHECK(html.find("&gt; Pick me") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: an empty menu says so instead of drawing a bare header")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Build();

    CHECK(RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations).find("Nothing here") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: pagination footer appears only once a menu spans multiple pages")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuBuilder builder("Multi-row Menu");
    for (int i = 0; i < ItemsPerPage; ++i)
        builder.Button("Row", [](int) {});
    auto menu = builder.Build();

    // Exactly one page: no page indicator, no [A/D] page hint.
    std::string onePage = RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations);
    CHECK(onePage.find("(1/") == std::string::npos);
    CHECK(onePage.find("Page") == std::string::npos);

    menu->Items.push_back(ButtonRow{.Label = "Row"}.ToItem());  // spills to page 2
    std::string twoPages = RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations);
    CHECK(twoPages.find("(1/2)") != std::string::npos);
    CHECK(twoPages.find("[A/D]") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: a submenu shows the Back hint, a root menu shows Close")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Button("Row", [](int) {}).Build();

    CHECK(RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations).find("Close") != std::string::npos);
    CHECK(RenderMenuHtml(menu.get(), ViewOf(*menu, 0, true), translations).find("Back") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: a toggle draws its value as a switch")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Add(ToggleRow{.Label = "Prefix", .Get = [](int) { return true; }}).Build();

    CHECK(RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations).find("Prefix: [ON]") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: a choice row keeps the arrows that say A and D change it")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Add(ChoiceRow<int>{.Label = "Speed", .Choices = {{"100%", 100}}}).Build();

    CHECK(RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations).find("Speed: &lt; 100% &gt;") !=
          std::string::npos);
}

TEST_CASE("CenterHtmlRender: a pending row trails an ellipsis and a changed row a star")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"150 HP", 150}}}).Build();

    // What MenuManager::Describe hands a driver while a stepped value is waiting to be applied.
    CenterHtmlView view = ViewOf(*menu, 0, false);
    view.Describe = [&menu](int index) {
        MenuRow row = menu->Items[static_cast<std::size_t>(index)].Describe(0);
        row.Pending = true;
        row.Changed = true;
        return row;
    };

    const std::string html = RenderMenuHtml(menu.get(), view, translations);
    CHECK(html.find("HP: &lt; 150 HP… &gt; *") != std::string::npos);
}

TEST_CASE("CenterHtmlRender: row text is escaped, so a player name cannot inject markup")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Button("<b>Bold</b> & Co", [](int) {}).Build();

    std::string html = RenderMenuHtml(menu.get(), ViewOf(*menu, 0, false), translations);
    CHECK(html.find("&lt;b&gt;Bold&lt;/b&gt; &amp; Co") != std::string::npos);
    CHECK(html.find("<b>Bold</b>") == std::string::npos);
}
