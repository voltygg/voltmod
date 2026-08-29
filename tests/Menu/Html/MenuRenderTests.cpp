#include "Menu/Html/MenuRenderer.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/MenuBuilder.hpp>
#include <doctest/doctest.h>
#include <memory>
#include <string>

using VoltMod::ButtonRow;
using VoltMod::ChoiceRow;
using VoltMod::DefaultFooter;
using VoltMod::DefaultHeader;
using VoltMod::ItemsPerPage;
using VoltMod::MenuBuilder;
using VoltMod::RenderMenuHtml;
using VoltMod::SlotEvents;
using VoltMod::ToggleRow;
using VoltMod::Translations;

TEST_CASE("MenuRenderer: DefaultHeader shows the title and hides the page count for one page")
{
    CHECK(DefaultHeader("Admin Panel", "", 0, 1).find("Admin Panel") != std::string::npos);
    CHECK(DefaultHeader("Admin Panel", "", 0, 1).find("(1/1)") == std::string::npos);
    CHECK(DefaultHeader("Admin Panel", "", 1, 3).find("(2/3)") != std::string::npos);
}

TEST_CASE("MenuRenderer: a subtitle rides next to the title, and an empty one adds nothing")
{
    CHECK(DefaultHeader("Admin Panel", "v1.2.0", 0, 1).find("v1.2.0") != std::string::npos);
    CHECK(DefaultHeader("Admin Panel", "", 0, 1) == DefaultHeader("Admin Panel", "", 0, 1));
    CHECK(DefaultHeader("Admin Panel", "", 0, 1).length() < DefaultHeader("Admin Panel", "v1.2.0", 0, 1).length());
}

TEST_CASE("MenuRenderer: RenderMenuHtml marks the selected row and dims disabled rows")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu")
                    .Button("Enabled Row", [](int) {})
                    .Add(ButtonRow{.Label = "Disabled Row", .Enabled = false})
                    .Build();

    std::string html = RenderMenuHtml(menu.get(), 0, 0, false, translations);
    CHECK(html.find("Test Menu") != std::string::npos);
    CHECK(html.find("&gt; Enabled Row") != std::string::npos);  // cursor marker on the selected row
    CHECK(html.find("Disabled Row") != std::string::npos);
    // The disabled row is dimmed and carries no cursor marker of its own.
    CHECK(html.find("&gt; Disabled Row") == std::string::npos);
}

TEST_CASE("MenuRenderer: a non-selectable Text row renders without a cursor")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Text("Just a heading").Button("Pick me", [](int) {}).Build();

    // Selection lands on index 1: HtmlMenuManager::OpenMenu skips non-selectable rows when it opens a
    // menu, and this render call mirrors that already-adjusted index.
    std::string html = RenderMenuHtml(menu.get(), 0, 1, false, translations);
    CHECK(html.find("Just a heading") != std::string::npos);
    CHECK(html.find("&gt; Just a heading") == std::string::npos);
    CHECK(html.find("&gt; Pick me") != std::string::npos);
}

TEST_CASE("MenuRenderer: pagination footer appears only once a menu spans multiple pages")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuBuilder builder("Multi-row Menu");
    for (int i = 0; i < ItemsPerPage; ++i)
        builder.Button("Row", [](int) {});
    auto menu = builder.Build();

    // Exactly one page: no page indicator, no [A/D] page hint.
    std::string onePage = RenderMenuHtml(menu.get(), 0, 0, false, translations);
    CHECK(onePage.find("(1/") == std::string::npos);
    CHECK(onePage.find("Page") == std::string::npos);

    menu->Items.push_back(ButtonRow{.Label = "Row"}.ToItem());  // spills to page 2
    std::string twoPages = RenderMenuHtml(menu.get(), 0, 0, false, translations);
    CHECK(twoPages.find("(1/2)") != std::string::npos);
    CHECK(twoPages.find("[A/D]") != std::string::npos);
}

TEST_CASE("MenuRenderer: a submenu shows the Back hint, a root menu shows Close")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Button("Row", [](int) {}).Build();

    CHECK(RenderMenuHtml(menu.get(), 0, 0, false, translations).find("Close") != std::string::npos);
    CHECK(RenderMenuHtml(menu.get(), 0, 0, true, translations).find("Back") != std::string::npos);
}

TEST_CASE("MenuRenderer: a row that carries a value renders as title and value")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Add(ToggleRow{.Label = "Prefix", .Get = [](int) { return true; }}).Build();

    CHECK(RenderMenuHtml(menu.get(), 0, 0, false, translations).find("Prefix: ON") != std::string::npos);
}

TEST_CASE("MenuRenderer: a choice row keeps the arrows that say A and D change it")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Add(ChoiceRow<int>{.Label = "Speed", .Choices = {{"100%", 100}}}).Build();

    CHECK(RenderMenuHtml(menu.get(), 0, 0, false, translations).find("Speed: &lt; 100% &gt;") != std::string::npos);
}

TEST_CASE("MenuRenderer: row text is escaped, so a player name cannot inject markup")
{
    SlotEvents slots;
    Translations translations(slots);

    auto menu = MenuBuilder("Test Menu").Button("<b>Bold</b> & Co", [](int) {}).Build();

    std::string html = RenderMenuHtml(menu.get(), 0, 0, false, translations);
    CHECK(html.find("&lt;b&gt;Bold&lt;/b&gt; &amp; Co") != std::string::npos);
    CHECK(html.find("<b>Bold</b>") == std::string::npos);
}
