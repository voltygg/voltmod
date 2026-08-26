#include <VoltMod/Core/EffectManager.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <doctest/doctest.h>

using VoltMod::EffectInstance;
using VoltMod::EffectManager;
using VoltMod::EffectScope;
using VoltMod::MaxPlayers;
using VoltMod::Scheduler;

static constexpr int Disco = 0;
static constexpr int Ghost = 1;

// Apply with just an OnStop: state-only, no tick, no duration.
static void ApplyStateOnly(EffectManager& mgr, int slot, int id, std::function<void()> onStop)
{
    mgr.Apply(slot, id, EffectInstance{.OnStop = std::move(onStop)}, EffectScope::Persistent, 0, 0);
}

TEST_CASE("EffectManager: Apply and IsActive")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    CHECK(!mgr.IsActive(3, Disco));
    ApplyStateOnly(mgr, 3, Disco, [] {});
    CHECK(mgr.IsActive(3, Disco));
    CHECK(!mgr.IsActive(3, Ghost));
    CHECK(!mgr.IsActive(4, Disco));
}

TEST_CASE("EffectManager: re-Apply runs the prior onStop first")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    ApplyStateOnly(mgr, 3, Disco, [&] { ++cancels; });
    ApplyStateOnly(mgr, 3, Disco, [&] { cancels += 10; });
    CHECK_EQ(cancels, 1);  // first instance undone, second still active

    mgr.Cancel(3, Disco);
    CHECK_EQ(cancels, 11);
}

TEST_CASE("EffectManager: Cancel clears state and flips IsActive")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    ApplyStateOnly(mgr, 3, Disco, [&] { ++cancels; });
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
    ApplyStateOnly(mgr, 3, Disco, [&] { ++cancels; });
    ApplyStateOnly(mgr, 3, Ghost, [&] { ++cancels; });

    mgr.Cancel(3, Disco);
    mgr.Cancel(3, Disco);  // no-op: already gone
    CHECK_EQ(cancels, 1);
    CHECK(mgr.IsActive(3, Ghost));
}

TEST_CASE("EffectManager: CancelRound only touches round-scoped effects")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    mgr.Apply(3, Disco, EffectInstance{}, EffectScope::Round, 0, 0);
    mgr.Apply(3, Ghost, EffectInstance{}, EffectScope::Persistent, 0, 0);
    mgr.Apply(5, Disco, EffectInstance{}, EffectScope::Round, 0, 0);

    mgr.CancelRound();
    CHECK(!mgr.IsActive(3, Disco));
    CHECK(mgr.IsActive(3, Ghost));
    CHECK(!mgr.IsActive(5, Disco));
}

TEST_CASE("EffectManager: CancelAll(slot) and CancelAll()")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    mgr.Apply(3, Disco, EffectInstance{}, EffectScope::Persistent, 0, 0);
    mgr.Apply(3, Ghost, EffectInstance{}, EffectScope::Persistent, 0, 0);
    mgr.Apply(5, Disco, EffectInstance{}, EffectScope::Persistent, 0, 0);

    mgr.CancelAll(3);
    CHECK(!mgr.IsActive(3, Disco));
    CHECK(!mgr.IsActive(3, Ghost));
    CHECK(mgr.IsActive(5, Disco));

    mgr.CancelAll();
    CHECK(!mgr.IsActive(5, Disco));
}

TEST_CASE("EffectManager: CancelOnDeath keeps only Session-scoped effects")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    int cancels = 0;
    mgr.Apply(3, Disco, EffectInstance{.OnStop = [&] { ++cancels; }}, EffectScope::Persistent, 0, 0);
    mgr.Apply(3, Ghost, EffectInstance{.OnStop = [&] { ++cancels; }}, EffectScope::Session, 0, 0);
    mgr.Apply(5, Disco, EffectInstance{.OnStop = [&] { ++cancels; }}, EffectScope::Persistent, 0, 0);

    mgr.CancelOnDeath(3);
    CHECK(!mgr.IsActive(3, Disco));  // per-life: swept on death
    CHECK(mgr.IsActive(3, Ghost));   // Session: kept
    CHECK(mgr.IsActive(5, Disco));   // other slot untouched
    CHECK_EQ(cancels, 1);

    mgr.CancelOnDeath(-1);  // out-of-range: no-op
    mgr.CancelOnDeath(MaxPlayers);
    CHECK(mgr.IsActive(3, Ghost));
}

TEST_CASE("EffectManager: out-of-range slots are no-ops")
{
    Scheduler scheduler;
    EffectManager mgr(scheduler);

    ApplyStateOnly(mgr, -1, Disco, [] {});
    ApplyStateOnly(mgr, MaxPlayers, Disco, [] {});
    CHECK(!mgr.IsActive(-1, Disco));
    CHECK(!mgr.IsActive(MaxPlayers, Disco));
    mgr.Cancel(-1, Disco);
    mgr.CancelAll(MaxPlayers);  // must not crash
}
