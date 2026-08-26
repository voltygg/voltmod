#include "Menu/MenuRenderer.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/Menu.hpp>
#include <VoltMod/Menu/Options/ButtonOption.hpp>
#include <VoltMod/Menu/Options/TextOption.hpp>
#include <doctest/doctest.h>
#include <memory>

using VoltMod::ButtonOption;
using VoltMod::DefaultFooter;
using VoltMod::DefaultHeader;
using VoltMod::ItemsPerPage;
using VoltMod::MenuView;
using VoltMod::RenderMenuHtml;
using VoltMod::SlotEvents;
using VoltMod::TextOption;
using VoltMod::Translations;

TEST_CASE("MenuRenderer: DefaultHeader shows the title and hides the page count for one page")
{
    CHECK(DefaultHeader("Admin Panel", 0, 1).find("Admin Panel") != std::string::npos);
    CHECK(DefaultHeader("Admin Panel", 0, 1).find("(1/1)") == std::string::npos);
    CHECK(DefaultHeader("Admin Panel", 1, 3).find("(2/3)") != std::string::npos);
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

    // Selection lands on index 1: MenuManager::OpenMenu skips non-selectable rows when it opens a
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
