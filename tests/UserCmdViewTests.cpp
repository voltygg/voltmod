// UserCmd.hpp is deliberately protobuf- and SDK-free so this TU compiles it standalone;
// an include that drags in HL2SDK would fail to build here rather than silently pass.
#include <CS2Kit/Sdk/UserCmd.hpp>
#include <doctest/doctest.h>

using CS2Kit::Sdk::UserCmdView;

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

    REQUIRE(cmd.SampleAt(0) != nullptr);
    CHECK(cmd.SampleAt(0)->ViewYaw == doctest::Approx(0.0f));
    REQUIRE(cmd.SampleAt(3) != nullptr);
    CHECK(cmd.SampleAt(3)->ViewYaw == doctest::Approx(3.0f));
    CHECK(cmd.SampleAt(3) == &cmd.InputHistorySamples[3]);
}

TEST_CASE("SampleAt rejects the index one past the decoded count")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK(cmd.SampleAt(4) == nullptr);
    CHECK(cmd.SampleAt(5) == nullptr);
    CHECK(cmd.SampleAt(UserCmdView::MaxInputHistory) == nullptr);
}

TEST_CASE("SampleAt rejects negative indices")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK(cmd.SampleAt(-1) == nullptr);
    CHECK(cmd.SampleAt(-100) == nullptr);
}

TEST_CASE("SampleAt on an empty history is always null")
{
    const UserCmdView cmd;

    CHECK(cmd.SampleAt(0) == nullptr);
    CHECK(cmd.SampleAt(-1) == nullptr);
}

TEST_CASE("AttackSampleMissing is false for indices that decoded")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK_FALSE(cmd.AttackSampleMissing(0));
    CHECK_FALSE(cmd.AttackSampleMissing(3));
}

TEST_CASE("AttackSampleMissing is true from the decoded count onward")
{
    const UserCmdView cmd = WithHistory(4, 4);

    CHECK(cmd.AttackSampleMissing(4));
    CHECK(cmd.AttackSampleMissing(9));
}

TEST_CASE("AttackSampleMissing covers entries never sent as well as ones capped away")
{
    // 20 sent, MaxInputHistory decoded: 18 was dropped by the cap, 25 was never sent at all.
    const UserCmdView cmd = WithHistory(UserCmdView::MaxInputHistory, 20);

    CHECK(cmd.AttackSampleMissing(18));
    CHECK(cmd.AttackSampleMissing(25));
    // The predicate alone does not separate the two; InputHistoryTotalCount does.
    CHECK(18 < cmd.InputHistoryTotalCount);
    CHECK(25 >= cmd.InputHistoryTotalCount);
}

TEST_CASE("AttackSampleMissing treats no-attack as present rather than missing")
{
    const UserCmdView cmd = WithHistory(4, 4);

    // -1 means the command started no attack, which is not a dropped entry.
    CHECK_FALSE(cmd.AttackSampleMissing(-1));
}

TEST_CASE("A capped-away attack index reports missing instead of clamping")
{
    // The client sent 20 entries and the cap kept the first MaxInputHistory of them.
    const UserCmdView cmd = WithHistory(UserCmdView::MaxInputHistory, 20);

    CHECK(cmd.InputHistoryTotalCount > cmd.InputHistorySampleCount);
    CHECK(cmd.AttackSampleMissing(18));
    CHECK(cmd.SampleAt(18) == nullptr);
    // The last decoded entry is still addressable - the cap only drops the tail.
    CHECK_FALSE(cmd.AttackSampleMissing(UserCmdView::MaxInputHistory - 1));
    CHECK(cmd.SampleAt(UserCmdView::MaxInputHistory - 1) != nullptr);
}

TEST_CASE("An empty history reports every attack index as missing")
{
    const UserCmdView cmd = WithHistory(0, 6);

    CHECK(cmd.AttackSampleMissing(0));
    CHECK(cmd.SampleAt(0) == nullptr);
}
