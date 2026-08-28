#include "Menu/Html/MenuRenderer.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/Options/ButtonOption.hpp>
#include <VoltMod/Menu/Options/ChoiceOption.hpp>
#include <VoltMod/Menu/Options/TextOption.hpp>
#include <VoltMod/Menu/Options/ToggleOption.hpp>
#include <doctest/doctest.h>
#include <memory>

using VoltMod::ButtonOption;
using VoltMod::ChoiceOption;
using VoltMod::DefaultFooter;
using VoltMod::DefaultHeader;
using VoltMod::ItemsPerPage;
using VoltMod::MenuView;
using VoltMod::RenderMenuHtml;
using VoltMod::SlotEvents;
using VoltMod::TextOption;
using VoltMod::ToggleOption;
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

    MenuView menu;
    menu.Title = "Test Menu";
    menu.Items.push_back(std::make_shared<ButtonOption>("Enabled Row", [](int) {}, true));
    menu.Items.push_back(std::make_shared<ButtonOption>("Disabled Row", [](int) {}, false));

    std::string html = RenderMenuHtml(&menu, 0, 0, false, translations);
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

    MenuView menu;
    menu.Title = "Test Menu";
    menu.Items.push_back(std::make_shared<TextOption>("Just a heading"));
    menu.Items.push_back(std::make_shared<ButtonOption>("Pick me", [](int) {}, true));

    // Selection lands on index 1: HtmlMenuManager::OpenMenu skips non-selectable rows when it opens a
    // menu, and this render call mirrors that already-adjusted index.
    std::string html = RenderMenuHtml(&menu, 0, 1, false, translations);
    CHECK(html.find("Just a heading") != std::string::npos);
    CHECK(html.find("&gt; Just a heading") == std::string::npos);
    CHECK(html.find("&gt; Pick me") != std::string::npos);
}

TEST_CASE("MenuRenderer: pagination footer appears only once a menu spans multiple pages")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuView menu;
    menu.Title = "Multi-row Menu";
    for (int i = 0; i < ItemsPerPage; ++i)
        menu.Items.push_back(std::make_shared<ButtonOption>("Row", [](int) {}, true));

    // Exactly one page: no page indicator, no [A/D] page hint.
    std::string onePage = RenderMenuHtml(&menu, 0, 0, false, translations);
    CHECK(onePage.find("(1/") == std::string::npos);
    CHECK(onePage.find("Page") == std::string::npos);

    menu.Items.push_back(std::make_shared<ButtonOption>("Row", [](int) {}, true));  // spills to page 2
    std::string twoPages = RenderMenuHtml(&menu, 0, 0, false, translations);
    CHECK(twoPages.find("(1/2)") != std::string::npos);
    CHECK(twoPages.find("[A/D]") != std::string::npos);
}

TEST_CASE("MenuRenderer: a submenu shows the Back hint, a root menu shows Close")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuView menu;
    menu.Title = "Test Menu";
    menu.Items.push_back(std::make_shared<ButtonOption>("Row", [](int) {}, true));

    CHECK(RenderMenuHtml(&menu, 0, 0, false, translations).find("Close") != std::string::npos);
    CHECK(RenderMenuHtml(&menu, 0, 0, true, translations).find("Back") != std::string::npos);
}

TEST_CASE("MenuRenderer: a row that carries a value renders as title and value")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuView menu;
    menu.Title = "Test Menu";
    menu.Items.push_back(std::make_shared<ToggleOption>("Prefix", "ON", "OFF", [](int) { return true; }, [](int) {}));

    CHECK(RenderMenuHtml(&menu, 0, 0, false, translations).find("Prefix: ON") != std::string::npos);
}

TEST_CASE("MenuRenderer: a choice row keeps the arrows that say A and D change it")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuView menu;
    menu.Title = "Test Menu";
    std::vector<ChoiceOption<int>::Choice> choices{{.Label = "100%", .Value = 100}};
    menu.Items.push_back(std::make_shared<ChoiceOption<int>>("Speed", std::move(choices), [](int, const int&) {}));

    CHECK(RenderMenuHtml(&menu, 0, 0, false, translations).find("Speed: &lt; 100% &gt;") != std::string::npos);
}

TEST_CASE("MenuRenderer: row text is escaped, so a player name cannot inject markup")
{
    SlotEvents slots;
    Translations translations(slots);

    MenuView menu;
    menu.Title = "Test Menu";
    menu.Items.push_back(std::make_shared<ButtonOption>("<b>Bold</b> & Co", [](int) {}, true));

    std::string html = RenderMenuHtml(&menu, 0, 0, false, translations);
    CHECK(html.find("&lt;b&gt;Bold&lt;/b&gt; &amp; Co") != std::string::npos);
    CHECK(html.find("<b>Bold</b>") == std::string::npos);
}
