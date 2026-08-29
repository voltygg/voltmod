#include "FakeMenuSession.hpp"

#include <VoltMod/Menu/MenuBuilder.hpp>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using VoltMod::ButtonRow;
using VoltMod::ChoiceRow;
using VoltMod::InputRow;
using VoltMod::MenuBuilder;
using VoltMod::MenuItem;
using VoltMod::MenuRow;
using VoltMod::MenuRowKind;
using VoltMod::SubmenuRow;
using VoltMod::TextRow;
using VoltMod::ToggleRow;
using VoltModTests::FakeMenuSession;

TEST_CASE("MenuBuilder: a button row describes itself as a button and runs on activate")
{
    FakeMenuSession session;

    int ran = 0;
    MenuItem item = ButtonRow{.Label = "Kick", .Activate = [&](int slot) { ran = slot; }}.ToItem();

    const MenuRow row = item.Describe(3);
    CHECK(row.Label == "Kick");
    CHECK(row.Value.empty());
    CHECK(row.Kind == MenuRowKind::Button);
    CHECK(row.Enabled);
    CHECK(row.Selectable);
    CHECK_FALSE(row.Steppable);
    CHECK_FALSE(row.State.has_value());

    item.Activate(3, session);
    CHECK(ran == 3);
    CHECK_FALSE(static_cast<bool>(item.Step));
}

TEST_CASE("MenuBuilder: a disabled button describes itself disabled and does not run")
{
    FakeMenuSession session;

    int ran = 0;
    MenuItem item = ButtonRow{.Label = "Kick", .Activate = [&](int) { ++ran; }, .Enabled = false}.ToItem();

    CHECK_FALSE(item.Describe(0).Enabled);
    item.Activate(0, session);
    CHECK(ran == 0);
}

TEST_CASE("MenuBuilder: a toggle carries its state and flips on both activate and step")
{
    FakeMenuSession session;

    bool state = false;
    MenuItem item =
        ToggleRow{.Label = "God mode", .Get = [&](int) { return state; }, .Flip = [&](int) { state = !state; }}
            .ToItem();

    MenuRow row = item.Describe(0);
    CHECK(row.Kind == MenuRowKind::Toggle);
    CHECK(row.Value == "OFF");
    CHECK(row.Steppable);
    REQUIRE(row.State.has_value());
    CHECK_FALSE(*row.State);

    item.Activate(0, session);
    row = item.Describe(0);
    CHECK(row.Value == "ON");
    CHECK(*row.State);

    CHECK(item.Step(0, -1));
    CHECK(item.Describe(0).Value == "OFF");
}

TEST_CASE("MenuBuilder: a toggle uses the labels it was given")
{
    MenuItem item =
        ToggleRow{.Label = "Mode", .On = "Enabled", .Off = "Disabled", .Get = [](int) { return true; }}.ToItem();

    CHECK(item.Describe(0).Value == "Enabled");
}

TEST_CASE("MenuBuilder: a choice row wraps in both directions and shows the current label")
{
    MenuItem item = ChoiceRow<int>{.Label = "HP", .Choices = {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}}}.ToItem();

    MenuRow row = item.Describe(0);
    CHECK(row.Kind == MenuRowKind::Choice);
    CHECK(row.Value == "1 HP");
    CHECK(row.Steppable);

    CHECK(item.Step(0, -1));
    CHECK(item.Describe(0).Value == "999 HP");
    CHECK(item.Step(0, +1));
    CHECK(item.Describe(0).Value == "1 HP");
}

TEST_CASE("MenuBuilder: a choice row commits the current value on activate and on commit")
{
    FakeMenuSession session;

    std::vector<int> committed;
    MenuItem item = ChoiceRow<int>{.Label = "HP",
                                   .Choices = {{"1 HP", 1}, {"100 HP", 100}},
                                   .Commit = [&](int, const int& value) { committed.push_back(value); },
                                   .Index = 1}
                        .ToItem();

    CHECK(item.Describe(0).Value == "100 HP");

    item.Activate(0, session);
    REQUIRE(committed.size() == 1);
    CHECK(committed[0] == 100);

    // Stepping alone does not apply; Commit is what applies whatever the row now shows.
    CHECK(item.Step(0, +1));
    CHECK(committed.size() == 1);
    item.Commit(0);
    REQUIRE(committed.size() == 2);
    CHECK(committed[1] == 1);
}

TEST_CASE("MenuBuilder: an OnSelect choice row carries no commit for the manager to hold")
{
    FakeMenuSession session;

    std::vector<int> committed;
    MenuItem item = ChoiceRow<int>{.Label = "HP",
                                   .Choices = {{"1 HP", 1}, {"100 HP", 100}},
                                   .Commit = [&](int, const int& value) { committed.push_back(value); },
                                   .Apply = VoltMod::ChoiceApply::OnSelect}
                        .ToItem();

    // No MenuItem::Commit is what tells the manager this row does not apply while it is cycled.
    CHECK_FALSE(static_cast<bool>(item.Commit));

    CHECK(item.Step(0, +1));
    CHECK(committed.empty());

    item.Activate(0, session);
    REQUIRE(committed.size() == 1);
    CHECK(committed[0] == 100);
}

TEST_CASE("MenuBuilder: a choice row with no commit callback steps forward on activate")
{
    FakeMenuSession session;

    MenuItem item = ChoiceRow<std::string>{.Label = "Color", .Choices = {{"Red", "red"}, {"Blue", "blue"}}}.ToItem();

    item.Activate(0, session);
    CHECK(item.Describe(0).Value == "Blue");
}

TEST_CASE("MenuBuilder: a choice row round-trips an external index")
{
    int index = 0;
    MenuItem item = ChoiceRow<int>{.Label = "HP",
                                   .Choices = {{"1 HP", 1}, {"100 HP", 100}, {"999 HP", 999}},
                                   .GetIndex = [&](int) { return index; },
                                   .SetIndex = [&](int, int value) { index = value; }}
                        .ToItem();

    CHECK(item.Step(0, +1));
    CHECK(index == 1);
    index = 2;
    CHECK(item.Describe(0).Value == "999 HP");
}

TEST_CASE("MenuBuilder: an input row rejects text over its maximum without reaching the setter")
{
    FakeMenuSession session;

    int sets = 0;
    MenuItem item = InputRow{.Label = "Reason",
                             .Prompt = "Type a reason",
                             .Get = [](int) { return std::string("cheating"); },
                             .Set =
                                 [&](int, std::string_view) {
                                     ++sets;
                                     return true;
                                 },
                             .MaxLength = 4}
                        .ToItem();

    const MenuRow row = item.Describe(0);
    CHECK(row.Kind == MenuRowKind::Input);
    CHECK(row.Value == "cheating");

    item.Activate(0, session);
    REQUIRE(session.Prompts == 1);
    CHECK(session.LastPrompt == "Type a reason");
    REQUIRE(static_cast<bool>(session.LastInput));

    CHECK_FALSE(session.LastInput(0, "far too long"));
    CHECK(sets == 0);
    CHECK(session.LastInput(0, "ok"));
    CHECK(sets == 1);
}

TEST_CASE("MenuBuilder: an input row with no value shows a placeholder")
{
    MenuItem item = InputRow{.Label = "Reason"}.ToItem();
    CHECK(item.Describe(0).Value == "…");
}

TEST_CASE("MenuBuilder: a submenu row opens what its factory built, and nothing when it built none")
{
    FakeMenuSession session;

    MenuItem item = SubmenuRow{.Label = "More", .Build = [](int) { return MenuBuilder("Submenu").Build(); }}.ToItem();

    CHECK(item.Describe(0).Kind == MenuRowKind::Submenu);
    item.Activate(0, session);
    REQUIRE(session.Opened.size() == 1);
    REQUIRE(session.Last() != nullptr);
    CHECK(session.Last()->Title == "Submenu");

    MenuItem empty =
        SubmenuRow{.Label = "More", .Build = [](int) { return std::shared_ptr<VoltMod::Menu>{}; }}.ToItem();
    empty.Activate(0, session);
    CHECK(session.Opened.size() == 1);
}

TEST_CASE("MenuBuilder: a text row is not selectable and does nothing when activated")
{
    MenuItem item = TextRow{.Label = "Punish"}.ToItem();

    const MenuRow row = item.Describe(0);
    CHECK(row.Kind == MenuRowKind::Text);
    CHECK_FALSE(row.Selectable);
    CHECK_FALSE(static_cast<bool>(item.Activate));
}

TEST_CASE("MenuBuilder: the chain lands the title, the subtitle and every row in order")
{
    auto menu = MenuBuilder("Admin Panel")
                    .Subtitle("Bob")
                    .Text("Punish")
                    .Button("Kick", [](int) {})
                    .Add(ToggleRow{.Label = "God mode", .Get = [](int) { return true; }})
                    .Add(ChoiceRow<int>{.Label = "HP", .Choices = {{"100 HP", 100}}})
                    .Submenu("More", [](int) { return std::shared_ptr<VoltMod::Menu>{}; })
                    .Build();

    REQUIRE(static_cast<bool>(menu));
    CHECK(menu->Title == "Admin Panel");
    CHECK(menu->Subtitle == "Bob");
    REQUIRE(menu->Items.size() == 5);
    CHECK(menu->Items[0].Describe(0).Kind == MenuRowKind::Text);
    CHECK(menu->Items[1].Describe(0).Label == "Kick");
    CHECK(menu->Items[2].Describe(0).Kind == MenuRowKind::Toggle);
    CHECK(menu->Items[3].Describe(0).Kind == MenuRowKind::Choice);
    CHECK(menu->Items[4].Describe(0).Kind == MenuRowKind::Submenu);
}
