#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <doctest/doctest.h>

using VoltMod::EffectManager;
using VoltMod::EffectSpec;
using VoltMod::MaxPlayers;
using VoltMod::Scheduler;

static constexpr int Disco = 0;
static constexpr int Ghost = 1;

TEST_CASE("EffectManager: Apply and IsActive")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    CHECK(!mgr.IsActive(3, Disco));
    mgr.Apply(3, Disco, {.OnStop = [] {}});
    CHECK(mgr.IsActive(3, Disco));
    CHECK(!mgr.IsActive(3, Ghost));
    CHECK(!mgr.IsActive(4, Disco));
}

TEST_CASE("EffectManager: re-Apply runs the prior onStop first")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    mgr.Apply(3, Disco, {.OnStop = [&] { ++cancels; }});
    mgr.Apply(3, Disco, {.OnStop = [&] { cancels += 10; }});
    CHECK_EQ(cancels, 1);  // first instance undone, second still active

    mgr.Cancel(3, Disco);
    CHECK_EQ(cancels, 11);
}

TEST_CASE("EffectManager: Cancel clears state and flips IsActive")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    mgr.Apply(3, Disco, {.OnStop = [&] { ++cancels; }});
    CHECK(mgr.IsActive(3, Disco));

    mgr.Cancel(3, Disco);
    CHECK(!mgr.IsActive(3, Disco));
    CHECK_EQ(cancels, 1);
}

TEST_CASE("EffectManager: Cancel is idempotent and per-id")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    mgr.Apply(3, Disco, {.OnStop = [&] { ++cancels; }});
    mgr.Apply(3, Ghost, {.OnStop = [&] { ++cancels; }});

    mgr.Cancel(3, Disco);
    mgr.Cancel(3, Disco);  // no-op: already gone
    CHECK_EQ(cancels, 1);
    CHECK(mgr.IsActive(3, Ghost));
}

TEST_CASE("EffectManager: CancelRoundScoped only touches round-scoped effects")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    mgr.Apply(3, Disco, {.RoundScoped = true, .OnStop = [] {}});
    mgr.Apply(3, Ghost, {.RoundScoped = false, .OnStop = [] {}});
    mgr.Apply(5, Disco, {.RoundScoped = true, .OnStop = [] {}});

    mgr.CancelRoundScoped();
    CHECK(!mgr.IsActive(3, Disco));
    CHECK(mgr.IsActive(3, Ghost));
    CHECK(!mgr.IsActive(5, Disco));
}

TEST_CASE("EffectManager: CancelAllForSlot and CancelAll")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    mgr.Apply(3, Disco, {.OnStop = [] {}});
    mgr.Apply(3, Ghost, {.OnStop = [] {}});
    mgr.Apply(5, Disco, {.OnStop = [] {}});

    mgr.CancelAllForSlot(3);
    CHECK(!mgr.IsActive(3, Disco));
    CHECK(!mgr.IsActive(3, Ghost));
    CHECK(mgr.IsActive(5, Disco));

    mgr.CancelAll();
    CHECK(!mgr.IsActive(5, Disco));
}

TEST_CASE("EffectManager: CancelPerLife keeps only SurvivesDeath effects")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    mgr.Apply(3, Disco, {.OnStop = [&] { ++cancels; }});
    mgr.Apply(3, Ghost, {.SurvivesDeath = true, .OnStop = [&] { ++cancels; }});
    mgr.Apply(5, Disco, {.OnStop = [&] { ++cancels; }});

    mgr.CancelPerLife(3);
    CHECK(!mgr.IsActive(3, Disco));  // per-life: swept on death
    CHECK(mgr.IsActive(3, Ghost));   // SurvivesDeath: kept
    CHECK(mgr.IsActive(5, Disco));   // other slot untouched
    CHECK_EQ(cancels, 1);

    mgr.CancelPerLife(-1);  // out-of-range: no-op
    mgr.CancelPerLife(MaxPlayers);
    CHECK(mgr.IsActive(3, Ghost));
}

TEST_CASE("EffectManager: out-of-range slots are no-ops")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    mgr.Apply(-1, Disco, {.OnStop = [] {}});
    mgr.Apply(MaxPlayers, Disco, {.OnStop = [] {}});
    CHECK(!mgr.IsActive(-1, Disco));
    CHECK(!mgr.IsActive(MaxPlayers, Disco));
    mgr.Cancel(-1, Disco);
    mgr.CancelAllForSlot(MaxPlayers);  // must not crash
}
