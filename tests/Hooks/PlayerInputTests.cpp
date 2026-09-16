// Keep UserCmd.hpp protobuf- and SDK-free so this standalone test does not require HL2SDK.
#include <VoltMod/Hooks/PlayerInput.hpp>
#include <doctest/doctest.h>

using VoltMod::PlayerInput;

static PlayerInput WithHistory(int decoded, int total)
{
    PlayerInput cmd;
    cmd.Valid = true;
    cmd.InputHistorySampleCount = decoded;
    cmd.InputHistoryTotalCount = total;
    for (int i = 0; i < decoded; ++i)
    {
        cmd.InputHistorySamples[i].HasViewAngles = true;
        cmd.InputHistorySamples[i].ViewYaw = static_cast<float>(i);
    }
    return cmd;
}

TEST_CASE("SampleAt returns the addressed entry inside the decoded range")
{
    const PlayerInput cmd = WithHistory(4, 4);

    REQUIRE(cmd.SampleAt(0).has_value());
    CHECK(cmd.SampleAt(0)->ViewYaw == doctest::Approx(0.0f));
    REQUIRE(cmd.SampleAt(3).has_value());
    CHECK(cmd.SampleAt(3)->ViewYaw == doctest::Approx(3.0f));
    CHECK(cmd.SampleAt(3)->ViewYaw == doctest::Approx(cmd.InputHistorySamples[3].ViewYaw));
}

TEST_CASE("SampleAt rejects the index one past the decoded count")
{
    const PlayerInput cmd = WithHistory(4, 4);

    CHECK(!cmd.SampleAt(4).has_value());
    CHECK(!cmd.SampleAt(5).has_value());
    CHECK(!cmd.SampleAt(PlayerInput::MaxInputHistory).has_value());
}

TEST_CASE("SampleAt rejects negative indices")
{
    const PlayerInput cmd = WithHistory(4, 4);

    CHECK(!cmd.SampleAt(-1).has_value());
    CHECK(!cmd.SampleAt(-100).has_value());
}

TEST_CASE("SampleAt on an empty history is always empty")
{
    const PlayerInput cmd;

    CHECK(!cmd.SampleAt(0).has_value());
    CHECK(!cmd.SampleAt(-1).has_value());
}

TEST_CASE("A capped-away attack index reads as absent instead of clamping")
{
    const PlayerInput cmd = WithHistory(PlayerInput::MaxInputHistory, 20);

    CHECK(cmd.InputHistoryTotalCount > cmd.InputHistorySampleCount);
    CHECK(!cmd.SampleAt(18).has_value());
    CHECK(cmd.SampleAt(PlayerInput::MaxInputHistory - 1).has_value());
}

TEST_CASE("InputHistoryTotalCount separates a capped-away entry from one never sent")
{
    const PlayerInput cmd = WithHistory(PlayerInput::MaxInputHistory, 20);

    CHECK(!cmd.SampleAt(18).has_value());
    CHECK(18 < cmd.InputHistoryTotalCount);
    CHECK(!cmd.SampleAt(25).has_value());
    CHECK(25 >= cmd.InputHistoryTotalCount);
}

TEST_CASE("An empty history has no addressable entry for any attack index")
{
    const PlayerInput cmd = WithHistory(0, 6);

    CHECK(!cmd.SampleAt(0).has_value());
    CHECK(0 < cmd.InputHistoryTotalCount);
}
