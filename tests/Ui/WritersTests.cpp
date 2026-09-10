#include <VoltMod/Ui/Writers.hpp>

#include "Support/FakePanel.hpp"

#include <doctest/doctest.h>
#include <array>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

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

TEST_CASE("PanelWriter names the panel and slot once")
{
    FakePanel panel;
    const VoltMod::PanelWriter<FakePanel> writer{panel, 3};
    const TextVar title{.Root = "card", .Var = "title"};
    const ClassFlag hidden{.Id = "card", .Class = "Hidden"};

    writer.Set(title, "Round 2");
    writer.Set(hidden, false);
    writer.Set(Icons, 2);

    CHECK(writer.Slot() == 3);
    REQUIRE(panel.Texts.size() == 1);
    CHECK(std::get<0>(panel.Texts[0]) == 3);
    REQUIRE(panel.Classes.size() == 4);
    for (const auto& [slot, id, cls, on] : panel.Classes)
        CHECK(slot == 3);
}

struct LabelId
{
    std::string_view Id;
    std::string_view Var;
};

struct LabelWriters
{
    TextVar Label;
    ClassFlag Hidden;
};

constexpr std::array<LabelId, 2> Labels{LabelId{"s_tab0", "tab0"}, LabelId{"s_tab1", "tab1"}};
constexpr auto TabWriters = VoltMod::MakeWriters(Labels, [](const LabelId& tab) {
    return LabelWriters{.Label = {"s", tab.Var}, .Hidden = {tab.Id, "Hidden"}};
});

static_assert(TabWriters.size() == 2);
static_assert(TabWriters[1].Hidden.Id == "s_tab1");
static_assert(TabWriters[1].Label.Var == "tab1");

TEST_CASE("MakeWriters builds one bundle per generated entry")
{
    FakePanel panel;
    TabWriters[0].Hidden.Write(panel, 0, true);
    CHECK(panel.Enabled() == std::vector<std::string>{"s_tab0.Hidden"});
}
