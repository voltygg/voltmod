#include "Support/FakePanel.hpp"
#include "Ui/Fixtures/Lab.hpp"

#include <doctest/doctest.h>
#include <ostream>
#include <string>
#include <vector>

using VoltModTests::FakePanel;

/* The fixture header is derived from a card-and-toast screen by `voltmod panorama bind`, and
   cli/tests/test_bind.py regenerates it - so this is the emitted C++ being compiled and run. */

static_assert(LabUi::Layout == "lab");
static_assert(LabUi::Card.size() == 2);
static_assert(LabUi::Card[1].Bar.Count == 5);
static_assert(LabUi::Icon::Awp == static_cast<LabUi::Icon>(1));

TEST_CASE("A bound screen writes through every widget it derived")
{
    FakePanel panel;

    CHECK(LabUi::Hidden.Write(panel, 2, false));
    CHECK(LabUi::Card[0].Hidden.Write(panel, 2, true));
    CHECK(LabUi::Card[0].Title.Write(panel, 2, "AK-47"));
    CHECK(LabUi::Card[0].Subtitle.Write(panel, 2, "Rifle"));
    CHECK(LabUi::Card[0].Value.Write(panel, 2, "12"));
    CHECK(LabUi::Card[0].Accent.Write(panel, 2, static_cast<int>(LabUi::Accent::Good)));
    CHECK(LabUi::Card[0].Icon.Write(panel, 2, LabUi::Card[0].Icon.Find("awp")));
    CHECK(LabUi::Card[0].Bar.Write(panel, 2, 4));
    CHECK(LabUi::Card[1].Title.Write(panel, 2, "AWP"));
    CHECK(LabUi::Toast.Show.Write(panel, 2, true));
    CHECK(LabUi::Toast.Title.Write(panel, 2, "Round over"));
    CHECK(LabUi::Toast.Description.Write(panel, 2, "Terrorists win"));
    CHECK(LabUi::Toast.Accent.Write(panel, 2, static_cast<int>(LabUi::Accent::Bad)));

    const std::vector<std::string> enabled = panel.Enabled();
    CHECK(enabled
          == std::vector<std::string>{"lab_card0.Hidden", "lab_card0_accent.Accent--good",
                                      "lab_card0_icon.Icon--awp", "lab_card0_bar.Step--4",
                                      "lab_toast.Show", "lab_toast_accent.Accent--bad"});
}

TEST_CASE("A bound text widget names the layout root, not the panel")
{
    FakePanel panel;

    CHECK(LabUi::Card[1].Value.Write(panel, 0, "1"));

    REQUIRE(panel.Texts.size() == 1);
    const auto& [slot, id, var, value] = panel.Texts[0];
    CHECK(id == LabUi::RootId);
    CHECK(var == "card1_value");
    CHECK(slot == 0);
    CHECK(value == "1");
}

TEST_CASE("A bound family clears the classes it is not on")
{
    FakePanel panel;

    CHECK(LabUi::Card[0].Bar.Write(panel, 0, -1));

    CHECK(static_cast<int>(panel.Classes.size()) == LabUi::Card[0].Bar.Count);
    CHECK(panel.Enabled().empty());
}

TEST_CASE("A bound family finds its index by variant name")
{
    CHECK(LabUi::Toast.Accent.Find("bad") == static_cast<int>(LabUi::Accent::Bad));
    CHECK(LabUi::Toast.Accent.Find("Accent--good") == 0);
    CHECK(LabUi::Card[0].Icon.Find(LabUi::IconNames[1]) == 1);
    CHECK(LabUi::Card[0].Bar.Find("3") == 3);
    CHECK(LabUi::Card[0].Icon.Find("famas") == -1);
}
