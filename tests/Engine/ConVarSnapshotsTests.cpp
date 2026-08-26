#include <VoltMod/Engine/ConVarSnapshots.hpp>
#include <doctest/doctest.h>

using VoltMod::Engine::ConVarSnapshots;

TEST_CASE("Snapshots start empty")
{
    ConVarSnapshots saved;
    CHECK(saved.Size() == 0);
    CHECK_FALSE(saved.Contains("sv_gravity"));
    CHECK(saved.Entries().empty());
}

TEST_CASE("Saving records the value once")
{
    ConVarSnapshots saved;
    CHECK(saved.Save("sv_gravity", "800"));
    CHECK(saved.Contains("sv_gravity"));
    CHECK(saved.Size() == 1);
    CHECK(saved.Entries().front().Value == "800");
}

TEST_CASE("A second save keeps the first value")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");

    // The re-assert path: overriding an already-held convar must not overwrite the operator's
    // value with the override, or the restore would put the override back.
    CHECK_FALSE(saved.Save("sv_gravity", "250"));
    CHECK(saved.Size() == 1);
    CHECK(saved.Entries().front().Value == "800");
}

TEST_CASE("An empty name is refused")
{
    ConVarSnapshots saved;
    CHECK_FALSE(saved.Save("", "800"));
    CHECK(saved.Size() == 0);
    CHECK_FALSE(saved.Contains(""));
}

TEST_CASE("Removing hands back the saved value and forgets it")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");

    auto value = saved.Remove("sv_gravity");
    REQUIRE(value.has_value());
    CHECK(*value == "800");
    CHECK_FALSE(saved.Contains("sv_gravity"));
    CHECK(saved.Size() == 0);
}

TEST_CASE("Removing something never saved yields nothing")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");

    CHECK_FALSE(saved.Remove("sv_airaccelerate").has_value());
    CHECK(saved.Size() == 1);
}

TEST_CASE("Removing one snapshot leaves the others in order")
{
    ConVarSnapshots saved;
    saved.Save("first", "1");
    saved.Save("second", "2");
    saved.Save("third", "3");

    CHECK(saved.Remove("second").has_value());
    REQUIRE(saved.Size() == 2);
    CHECK(saved.Entries()[0].Name == "first");
    CHECK(saved.Entries()[1].Name == "third");
}

TEST_CASE("Entries keep the order they were taken")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");
    saved.Save("sv_airaccelerate", "12");
    saved.Save("sv_maxvelocity", "3500");

    REQUIRE(saved.Size() == 3);
    CHECK(saved.Entries()[0].Name == "sv_gravity");
    CHECK(saved.Entries()[1].Name == "sv_airaccelerate");
    CHECK(saved.Entries()[2].Name == "sv_maxvelocity");
}

TEST_CASE("Re-saving after a remove takes a fresh value")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");
    saved.Remove("sv_gravity");

    CHECK(saved.Save("sv_gravity", "600"));
    CHECK(saved.Entries().front().Value == "600");
}

TEST_CASE("Clearing drops everything")
{
    ConVarSnapshots saved;
    saved.Save("sv_gravity", "800");
    saved.Save("sv_airaccelerate", "12");

    saved.Clear();
    CHECK(saved.Size() == 0);
    CHECK_FALSE(saved.Contains("sv_gravity"));
}

TEST_CASE("Names are matched whole, not by prefix")
{
    ConVarSnapshots saved;
    saved.Save("sv_stamina", "1");

    CHECK_FALSE(saved.Contains("sv_stam"));
    CHECK_FALSE(saved.Contains("sv_staminajumpcost"));
    CHECK(saved.Contains("sv_stamina"));
}
