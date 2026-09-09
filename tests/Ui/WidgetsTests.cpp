#include <VoltMod/Ui/Widgets.hpp>

#include <doctest/doctest.h>
#include <string>
#include <tuple>
#include <vector>

using VoltMod::Flag;
using VoltMod::OneOf;
using VoltMod::Status;
using VoltMod::Text;

namespace
{

/** Records what a widget wrote instead of driving a real panel. */
struct FakePanel
{
    std::vector<std::tuple<int, std::string, std::string, std::string>> Texts;
    std::vector<std::tuple<int, std::string, std::string, bool>> Classes;
    /** Set to fail the next N `Class` calls, in order. */
    std::vector<bool> ClassFails;
    int ClassCalls = 0;

    Status Text(int slot, std::string_view id, std::string_view var, std::string_view value)
    {
        Texts.emplace_back(slot, std::string(id), std::string(var), std::string(value));
        return {};
    }

    Status Class(int slot, std::string_view id, std::string_view cls, bool on)
    {
        Classes.emplace_back(slot, std::string(id), std::string(cls), on);
        const bool fail = ClassCalls < static_cast<int>(ClassFails.size()) && ClassFails[ClassCalls];
        ++ClassCalls;
        if (fail)
            return std::unexpected(VoltMod::Error::Failed("boom"));
        return {};
    }
};

}  // namespace

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

TEST_CASE("OneOf turns on only the selected index and clears the rest")
{
    FakePanel panel;
    OneOf<3> widget{.Ids = {"icon0", "icon1", "icon2"}, .Classes = {"Selected", "Selected", "Selected"}};

    CHECK(widget.Write(panel, 0, 1));
    REQUIRE(panel.Classes.size() == 3);
    CHECK(panel.Classes[0] == std::make_tuple(0, std::string("icon0"), std::string("Selected"), false));
    CHECK(panel.Classes[1] == std::make_tuple(0, std::string("icon1"), std::string("Selected"), true));
    CHECK(panel.Classes[2] == std::make_tuple(0, std::string("icon2"), std::string("Selected"), false));
}

TEST_CASE("OneOf with -1 leaves every entry off")
{
    FakePanel panel;
    OneOf<3> widget{.Ids = {"icon0", "icon1", "icon2"}, .Classes = {"Selected", "Selected", "Selected"}};

    CHECK(widget.Write(panel, 0, -1));
    REQUIRE(panel.Classes.size() == 3);
    for (const auto& [slot, id, cls, on] : panel.Classes)
        CHECK_FALSE(on);
}

TEST_CASE("OneOf across three panels sharing one class name selects a single tab")
{
    FakePanel panel;
    OneOf<3> widget{.Ids = {"tab0", "tab1", "tab2"}, .Classes = {"Selected", "Selected", "Selected"}};

    CHECK(widget.Write(panel, 2, 2));
    REQUIRE(panel.Classes.size() == 3);
    CHECK(std::get<3>(panel.Classes[2]));
    CHECK_FALSE(std::get<3>(panel.Classes[0]));
    CHECK_FALSE(std::get<3>(panel.Classes[1]));
}

TEST_CASE("OneOf returns the first failing status but still writes every entry")
{
    FakePanel panel;
    panel.ClassFails = {false, true, true};
    OneOf<3> widget{.Ids = {"icon0", "icon1", "icon2"}, .Classes = {"Selected", "Selected", "Selected"}};

    const Status result = widget.Write(panel, 0, 0);
    REQUIRE_FALSE(result);
    CHECK(result.error().Detail == "boom");
    CHECK(panel.Classes.size() == 3);  // the second failure did not stop the third write
}
