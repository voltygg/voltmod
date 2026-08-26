// UserCmd.hpp is deliberately protobuf- and SDK-free so this TU compiles it standalone;
// an include that drags in HL2SDK would fail to build here rather than silently pass.
#include <VoltMod/Hooks/UserCmd.hpp>
#include <doctest/doctest.h>

using VoltMod::Hooks::UserCmdView;

namespace
{

/** A view holding @p decoded decoded entries out of @p total the client sent. */
UserCmdView WithHistory(int decoded, int total)
{
    UserCmdView cmd;
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

}  // namespace

TEST_CASE("SampleAt returns the addressed entry inside the decoded range")
{
    const UserCmdView cmd = WithHistory(4, 4);

    REQUIRE(cmd.SampleAt(0).has_value());
    CHECK(cmd.SampleAt(0)->ViewYaw == doctest::Approx(0.0f));
    REQUIRE(cmd.SampleAt(3).has_value());
    CHECK(cmd.SampleAt(3)->ViewYaw == doctest::Approx(3.0f));
    // Addressing is by index into the decoded array, so the two must agree.
    CHECK(cmd.SampleAt(3)->ViewYaw == doctest::Approx(cmd.InputHistorySamples[3].ViewYaw));
}

TEST_CASE("SampleAt rejects the index one past the decoded count")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK(!cmd.SampleAt(4).has_value());
    CHECK(!cmd.SampleAt(5).has_value());
    CHECK(!cmd.SampleAt(UserCmdView::MaxInputHistory).has_value());
}

TEST_CASE("SampleAt rejects negative indices")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK(!cmd.SampleAt(-1).has_value());
    CHECK(!cmd.SampleAt(-100).has_value());
}

TEST_CASE("SampleAt on an empty history is always empty")
{
    const UserCmdView cmd;

    CHECK(!cmd.SampleAt(0).has_value());
    CHECK(!cmd.SampleAt(-1).has_value());
}

TEST_CASE("A capped-away attack index reads as absent instead of clamping")
{
    // The client sent 20 entries and the cap kept the first MaxInputHistory of them.
    const UserCmdView cmd = WithHistory(UserCmdView::MaxInputHistory, 20);

    CHECK(cmd.InputHistoryTotalCount > cmd.InputHistorySampleCount);
    CHECK(!cmd.SampleAt(18).has_value());
    // The last decoded entry is still addressable - the cap only drops the tail.
    CHECK(cmd.SampleAt(UserCmdView::MaxInputHistory - 1).has_value());
}

TEST_CASE("InputHistoryTotalCount separates a capped-away entry from one never sent")
{
    // 20 sent, MaxInputHistory decoded: 18 was dropped by the cap, 25 was never sent at all.
    const UserCmdView cmd = WithHistory(UserCmdView::MaxInputHistory, 20);

    CHECK(!cmd.SampleAt(18).has_value());
    CHECK(18 < cmd.InputHistoryTotalCount);
    CHECK(!cmd.SampleAt(25).has_value());
    CHECK(25 >= cmd.InputHistoryTotalCount);
}

TEST_CASE("An empty history has no addressable entry for any attack index")
{
    const UserCmdView cmd = WithHistory(0, 6);

    CHECK(!cmd.SampleAt(0).has_value());
    CHECK(0 < cmd.InputHistoryTotalCount);
}
