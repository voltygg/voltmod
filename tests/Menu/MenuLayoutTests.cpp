#include "Menu/MenuLayout.hpp"

#include <doctest/doctest.h>
#include <string>
#include <string_view>

using VoltMod::DefaultMenuLayout;
using VoltMod::MenuButton;
using VoltMod::MenuLayoutSpec;
using VoltMod::MenuPress;
using VoltMod::MenuRowIds;
using VoltMod::MenuRowKind;
using VoltMod::ParseMenuButton;

namespace
{

/** A second screen, to prove nothing is hard-coded to the shipped one's name or its nav strip. */
constexpr MenuLayoutSpec Hud{
    .Layout = "hud",
    .Root = "hud",
    .RowPrefix = "hud_row",
    .RowsPerPage = 5,
    .SubtitleId = "hud_subtitle",
    .PagerId = "hud_pager",
    .PrevId = "hud_prev",
    .NextId = "hud_next",
    .BackId = "hud_back",
    .CloseId = "hud_close",
    .PromptId = "hud_prompt",
    .CancelId = "hud_cancel",
    .TitleVar = "title",
    .SubtitleVar = "subtitle",
    .BreadcrumbVar = "breadcrumb",
    .PageVar = "page",
    .PromptTextVar = "prompt_text",
    .PromptHintVar = "prompt_hint",
    .NavBackVar = "back",
    .NavCloseVar = "close",
    .NavCancelVar = "cancel",
    .NavCount = 0,
    .NavPrefix = "hud_nav",
};

}  // namespace

TEST_CASE("MenuLayout: a row's ids carry the screen name and its variables do not")
{
    const MenuRowIds row = VoltMod::RowIds(DefaultMenuLayout, 3);
    CHECK(row.Row == "voltmod_menu_row3");
    CHECK(row.Button == "voltmod_menu_row3_btn");
    CHECK(row.Dec == "voltmod_menu_row3_dec");
    CHECK(row.Inc == "voltmod_menu_row3_inc");
    CHECK(row.LabelVar == "row3_label");
    CHECK(row.ValueVar == "row3_value");

    const MenuRowIds other = VoltMod::RowIds(Hud, 0);
    CHECK(other.Row == "hud_row0");
    CHECK(other.Button == "hud_row0_btn");
    CHECK(other.LabelVar == "row0_label");

    CHECK(VoltMod::NavId(DefaultMenuLayout, 2) == "voltmod_menu_nav2");
    CHECK(VoltMod::NavVar(DefaultMenuLayout, 2) == "nav2");
}

TEST_CASE("MenuLayout: the shipped screen names the parts the renderer writes")
{
    // As std::string: doctest streams what it compares, and a string_view has no operator<< here.
    const auto text = [](std::string_view value) { return std::string(value); };

    CHECK(text(DefaultMenuLayout.Layout) == "voltmod_menu");
    CHECK(text(DefaultMenuLayout.Root) == "voltmod_menu");
    CHECK(DefaultMenuLayout.RowsPerPage == 10);
    CHECK(DefaultMenuLayout.NavCount == 8);
    CHECK(text(DefaultMenuLayout.SubtitleId) == "voltmod_menu_subtitle");
    CHECK(text(DefaultMenuLayout.PagerId) == "voltmod_menu_pager");
    CHECK(text(DefaultMenuLayout.PromptId) == "voltmod_menu_prompt");
    CHECK(text(DefaultMenuLayout.TitleVar) == "title");
    CHECK(text(DefaultMenuLayout.PromptTextVar) == "prompt_text");
    CHECK(text(DefaultMenuLayout.NavCancelVar) == "cancel");
}

TEST_CASE("MenuLayout: a row button carries its row index")
{
    const MenuPress press = ParseMenuButton(DefaultMenuLayout, "voltmod_menu_row3_btn");
    CHECK(press.Button == MenuButton::Row);
    CHECK(press.Row == 3);

    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_row0_btn").Row == 0);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_row9_btn").Row == 9);
}

TEST_CASE("MenuLayout: a row's steppers parse as their direction")
{
    const MenuPress dec = ParseMenuButton(DefaultMenuLayout, "voltmod_menu_row2_dec");
    CHECK(dec.Button == MenuButton::RowDec);
    CHECK(dec.Row == 2);

    const MenuPress inc = ParseMenuButton(DefaultMenuLayout, "voltmod_menu_row2_inc");
    CHECK(inc.Button == MenuButton::RowInc);
    CHECK(inc.Row == 2);
}

TEST_CASE("MenuLayout: the five nav ids parse, and carry no row")
{
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_back").Button == MenuButton::Back);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_close").Button == MenuButton::Close);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_prev").Button == MenuButton::Prev);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_next").Button == MenuButton::Next);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_cancel").Button == MenuButton::Cancel);
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_back").Row == -1);
}

TEST_CASE("MenuLayout: a nav tab parses as its index")
{
    const MenuPress press = ParseMenuButton(DefaultMenuLayout, "voltmod_menu_nav2");
    CHECK(press.Button == MenuButton::Nav);
    CHECK(press.Row == 2);

    // The index is whatever the id spelled; the host is what checks it against the strip's size.
    CHECK(ParseMenuButton(DefaultMenuLayout, "voltmod_menu_nav9").Button == MenuButton::Nav);

    // A screen without a nav strip has no such button.
    CHECK(ParseMenuButton(Hud, "hud_nav0").Button == MenuButton::None);
}

TEST_CASE("MenuLayout: anything else is no button at all")
{
    // The id is client-controlled text, so each of these is dropped rather than rounded to a row
    // or a nav button a press could land on.
    for (const char* id : {"", "voltmod_menu_row", "voltmod_menu_row_btn", "voltmod_menu_rowx_btn", "voltmod_menu_row3",
                           "voltmod_menu_row3_", "voltmod_menu_row3_x", "voltmod_menu_row03_btn",
                           "voltmod_menu_row+3_btn", "voltmod_menu_row-1_btn", "voltmod_menu_row 3_btn", "row3_btn",
                           "voltmod_menu_backk", "VOLTMOD_MENU_BACK", "voltmod_menu_row3_btn_btn", "voltmod_menu_nav",
                           "voltmod_menu_nav01", "voltmod_menu_nav-1", "voltmod_menu_nav2x"})
    {
        CAPTURE(id);
        const MenuPress press = ParseMenuButton(DefaultMenuLayout, id);
        CHECK(press.Button == MenuButton::None);
        CHECK(press.Row == -1);
    }
}

TEST_CASE("MenuLayout: one screen's ids are not another's")
{
    CHECK(ParseMenuButton(Hud, "voltmod_menu_row3_btn").Button == MenuButton::None);
    CHECK(ParseMenuButton(Hud, "voltmod_menu_back").Button == MenuButton::None);

    const MenuPress press = ParseMenuButton(Hud, "hud_row4_inc");
    CHECK(press.Button == MenuButton::RowInc);
    CHECK(press.Row == 4);
    CHECK(ParseMenuButton(Hud, "hud_close").Button == MenuButton::Close);
}

TEST_CASE("MenuLayout: every row kind has its own class")
{
    // As std::string: doctest streams what it compares, and a string_view has no operator<< here.
    const auto kindClass = [](MenuRowKind kind) { return std::string(VoltMod::MenuKindClass(kind)); };

    CHECK(kindClass(MenuRowKind::Text) == "Kind--text");
    CHECK(kindClass(MenuRowKind::Button) == "Kind--button");
    CHECK(kindClass(MenuRowKind::Submenu) == "Kind--submenu");
    CHECK(kindClass(MenuRowKind::Toggle) == "Kind--toggle");
    CHECK(kindClass(MenuRowKind::Choice) == "Kind--choice");
    CHECK(kindClass(MenuRowKind::Input) == "Kind--input");
}
