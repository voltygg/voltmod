#include <VoltMod/Ui/Writers.hpp>

#include "Support/FakePanel.hpp"

#include <doctest/doctest.h>
#include <array>
#include <string>
#include <string_view>
#include <tuple>

using VoltMod::ClassChoice;
using VoltMod::ClassFlag;
using VoltMod::TextVar;
using VoltModTests::FakePanel;

constexpr std::array<std::string_view, 3> IconClasses{"Icon--ak47", "Icon--awp", "Icon--m4a1"};
constexpr ClassChoice Icons{.Id = "card_icon", .Classes = IconClasses};

TEST_CASE("TextVar writes the root panel's dialog variable")
{
    FakePanel panel;
    TextVar writer{.Root = "card", .Var = "title"};

    writer.Write(panel, 3, "Round 2");
    REQUIRE(panel.Texts.size() == 1);
    CHECK(panel.Texts[0] == std::make_tuple(3, std::string("card"), std::string("title"), std::string("Round 2")));
}

TEST_CASE("ClassFlag toggles one class on and off")
{
    FakePanel panel;
    ClassFlag writer{.Id = "card", .Class = "Hidden"};

    writer.Write(panel, 1, true);
    writer.Write(panel, 1, false);
    REQUIRE(panel.Classes.size() == 2);
    CHECK(panel.Classes[0] == std::make_tuple(1, std::string("card"), std::string("Hidden"), true));
    CHECK(panel.Classes[1] == std::make_tuple(1, std::string("card"), std::string("Hidden"), false));
}

TEST_CASE("ClassChoice turns on only the selected class and clears the rest")
{
    FakePanel panel;

    Icons.Write(panel, 0, 1);
    REQUIRE(panel.Classes.size() == 3);
    CHECK(panel.Classes[0] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--ak47"), false));
    CHECK(panel.Classes[1] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--awp"), true));
    CHECK(panel.Classes[2] == std::make_tuple(0, std::string("card_icon"), std::string("Icon--m4a1"), false));
}

TEST_CASE("ClassChoice with None leaves every class off")
{
    FakePanel panel;

    Icons.Write(panel, 0, ClassChoice::None);
    REQUIRE(panel.Classes.size() == 3);
    for (const auto& [slot, id, cls, on] : panel.Classes)
        CHECK_FALSE(on);
}

static_assert(Icons.Count() == 3);
static_assert(Icons.Find("awp") == 1);

TEST_CASE("ClassChoice finds an index by class or by variant name")
{
    CHECK(Icons.Find("Icon--awp") == 1);
    CHECK(Icons.Find("awp") == 1);
    CHECK(Icons.Find("Icon--nope") == ClassChoice::None);
    CHECK(Icons.Find("") == ClassChoice::None);
}

TEST_CASE("ClassChoice writes every class so a stale one clears")
{
    FakePanel panel;
    Icons.Write(panel, 0, 0);
    CHECK(panel.Classes.size() == 3);
}
