#include <VoltMod/Ui/Widgets.hpp>

#include "Support/FakePanel.hpp"

#include <doctest/doctest.h>
#include <string>
#include <tuple>

using VoltMod::Flag;
using VoltMod::OneOf;
using VoltMod::Status;
using VoltMod::Text;
using VoltModTests::FakePanel;

constexpr OneOf<3> Icons{.Id = "card_icon", .Classes = {"Icon--ak47", "Icon--awp", "Icon--m4a1"}};

TEST_CASE("Text writes the root panel's dialog variable")
{
    FakePanel panel;
    Text widget{.Root = "card", .Var = "title"};

    CHECK(widget.Write(panel, 3, "Round 2"));
    REQUIRE(panel.Texts.size() == 1);
    CHECK(panel.Texts[0] == std::make_tuple(3, std::string("card"), std::string("title"), std::string("Round 2")));
}

TEST_CASE("Flag toggles one class on and off")
{
    FakePanel panel;
    Flag widget{.Id = "card", .Class = "Hidden"};

    CHECK(widget.Write(panel, 1, true));
    CHECK(widget.Write(panel, 1, false));
    REQUIRE(panel.Classes.size() == 2);
    CHECK(panel.Classes[0] == std::make_tuple(1, std::string("card"), std::string("Hidden"), true));
    CHECK(panel.Classes[1] == std::make_tuple(1, std::string("card"), std::string("Hidden"), false));
}

TEST_CASE("OneOf turns on only the selected class and clears the rest")
{
    FakePanel panel;

    CHECK(Icons.Write(panel, 0, 1));
    REQUIRE(panel.Classes.size() == 3);
    CHECK(panel.Classes[0] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--ak47"), false));
    CHECK(panel.Classes[1] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--awp"), true));
    CHECK(panel.Classes[2] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--m4a1"), false));
}

TEST_CASE("OneOf with -1 leaves every class off")
{
    FakePanel panel;

    CHECK(Icons.Write(panel, 0, -1));
    REQUIRE(panel.Classes.size() == 3);
    for (const auto& [slot, id, cls, on] : panel.Classes)
        CHECK_FALSE(on);
}

static_assert(Icons.Count == 3);
static_assert(Icons.Find("awp") == 1);

TEST_CASE("OneOf finds an index by class or by variant name")
{
    CHECK(Icons.Find("Icon--awp") == 1);
    CHECK(Icons.Find("awp") == 1);
    CHECK(Icons.Find("Icon--nope") == -1);
    CHECK(Icons.Find("") == -1);
}

TEST_CASE("OneOf returns the first failing status but still writes every class")
{
    FakePanel panel;
    panel.ClassFails = {false, true, true};

    const Status result = Icons.Write(panel, 0, 0);
    REQUIRE_FALSE(result);
    CHECK(result.error().Detail == "boom");
    CHECK(panel.Classes.size() == 3);  // the second failure did not stop the third write
}
