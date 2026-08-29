#include "Menu/PanoramaIds.hpp"

#include <doctest/doctest.h>

using VoltMod::MenuButton;
using VoltMod::MenuPress;
using VoltMod::ParseMenuButton;

TEST_CASE("PanoramaIds: a row button carries its row index")
{
    const MenuPress press = ParseMenuButton("vm_row3_btn");
    CHECK(press.Button == MenuButton::Row);
    CHECK(press.Row == 3);

    CHECK(ParseMenuButton("vm_row0_btn").Row == 0);
    CHECK(ParseMenuButton("vm_row7_btn").Row == 7);
}

TEST_CASE("PanoramaIds: a row's steppers parse as their direction")
{
    const MenuPress dec = ParseMenuButton("vm_row2_dec");
    CHECK(dec.Button == MenuButton::RowDec);
    CHECK(dec.Row == 2);

    const MenuPress inc = ParseMenuButton("vm_row2_inc");
    CHECK(inc.Button == MenuButton::RowInc);
    CHECK(inc.Row == 2);
}

TEST_CASE("PanoramaIds: the five nav ids parse, and carry no row")
{
    CHECK(ParseMenuButton("vm_back").Button == MenuButton::Back);
    CHECK(ParseMenuButton("vm_close").Button == MenuButton::Close);
    CHECK(ParseMenuButton("vm_prev").Button == MenuButton::Prev);
    CHECK(ParseMenuButton("vm_next").Button == MenuButton::Next);
    CHECK(ParseMenuButton("vm_cancel").Button == MenuButton::Cancel);
    CHECK(ParseMenuButton("vm_back").Row == -1);
}

TEST_CASE("PanoramaIds: anything else is no button at all")
{
    // The id is client-controlled text, so each of these is dropped rather than rounded to a row
    // or a nav button a press could land on.
    for (const char* id :
         {"", "vm_row", "vm_row_btn", "vm_rowx_btn", "vm_row3", "vm_row3_", "vm_row3_x", "vm_row03_btn", "vm_row+3_btn",
          "vm_row-1_btn", "vm_row 3_btn", "row3_btn", "vm_backk", "VM_BACK", "vm_row3_btn_btn"})
    {
        CAPTURE(id);
        const MenuPress press = ParseMenuButton(id);
        CHECK(press.Button == MenuButton::None);
        CHECK(press.Row == -1);
    }
}
