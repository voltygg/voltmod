#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <algorithm>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using VoltMod::ErrorCode;
using VoltMod::Event;
using VoltMod::VtableHook;

// VtableHook stores its remover as a plain function pointer, so the seam a test can inject is a
// free function over file-static state rather than a capturing lambda.
static std::vector<int> g_removed;

static void RecordRemoval(int hookId)
{
    g_removed.push_back(hookId);
}

/** Hands out @p ids in order, one per Add call, and counts how many times it was asked. */
class FakeAdder
{
public:
    explicit FakeAdder(std::vector<int> ids) : _ids(std::move(ids)) {}

    int operator()(bool post)
    {
        _posts.push_back(post);
        return _next < _ids.size() ? _ids[_next++] : 0;
    }

    size_t Calls() const { return _posts.size(); }
    const std::vector<bool>& Posts() const { return _posts; }

private:
    std::vector<int> _ids;
    std::vector<bool> _posts;
    size_t _next = 0;
};

TEST_CASE("A pre that SourceHook refuses installs nothing and never tries post")
{
    g_removed.clear();
    FakeAdder adder({0, 7});

    auto hook = VtableHook::Install("Test", true, true, [&](bool post) { return adder(post); }, &RecordRemoval);

    REQUIRE_FALSE(hook.has_value());
    CHECK(hook.error().Code == ErrorCode::Engine);
    CHECK(hook.error().Detail == "SourceHook refused the Test pre hook");
    CHECK(adder.Calls() == 1);
    CHECK(g_removed.empty());
}

TEST_CASE("A post that SourceHook refuses removes the pre that was already added")
{
    g_removed.clear();
    FakeAdder adder({5, 0});

    auto hook = VtableHook::Install("Test", true, true, [&](bool post) { return adder(post); }, &RecordRemoval);

    REQUIRE_FALSE(hook.has_value());
    CHECK(hook.error().Detail == "SourceHook refused the Test post hook");
    CHECK(adder.Calls() == 2);
    REQUIRE(g_removed.size() == 1);
    CHECK(g_removed[0] == 5);
}

TEST_CASE("A successful pair holds both ids and removes both when it goes away")
{
    g_removed.clear();
    FakeAdder adder({5, 7});

    {
        auto hook = VtableHook::Install("Test", true, true, [&](bool post) { return adder(post); }, &RecordRemoval);

        REQUIRE(hook.has_value());
        CHECK(static_cast<bool>(*hook));
        CHECK(adder.Calls() == 2);
        CHECK(adder.Posts() == std::vector<bool>{false, true});
        CHECK(g_removed.empty());
    }

    REQUIRE(g_removed.size() == 2);
    CHECK(std::count(g_removed.begin(), g_removed.end(), 5) == 1);
    CHECK(std::count(g_removed.begin(), g_removed.end(), 7) == 1);
}

TEST_CASE("One side alone is installed and removed on its own")
{
    g_removed.clear();
    FakeAdder adder({9});

    auto hook = VtableHook::Install("Test", false, true, [&](bool post) { return adder(post); }, &RecordRemoval);

    REQUIRE(hook.has_value());
    CHECK(adder.Calls() == 1);
    CHECK(adder.Posts() == std::vector<bool>{true});

    hook->Reset();
    REQUIRE(g_removed.size() == 1);
    CHECK(g_removed[0] == 9);
}

TEST_CASE("Reset removes each id exactly once however often it is called")
{
    g_removed.clear();
    FakeAdder adder({5, 7});
    auto hook = VtableHook::Install("Test", true, true, [&](bool post) { return adder(post); }, &RecordRemoval);
    REQUIRE(hook.has_value());

    hook->Reset();
    hook->Reset();

    CHECK_FALSE(static_cast<bool>(*hook));
    CHECK(g_removed.size() == 2);
}

TEST_CASE("Moving a hook transfers ownership of the removal")
{
    g_removed.clear();
    FakeAdder adder({5, 7});
    auto hook = VtableHook::Install("Test", true, true, [&](bool post) { return adder(post); }, &RecordRemoval);
    REQUIRE(hook.has_value());

    {
        VtableHook moved = std::move(*hook);
        CHECK(static_cast<bool>(moved));
        CHECK_FALSE(static_cast<bool>(*hook));
        CHECK(g_removed.empty());
    }

    CHECK(g_removed.size() == 2);

    // The moved-from hook owns nothing, so letting it go removes nothing more.
    hook->Reset();
    CHECK(g_removed.size() == 2);
}

TEST_CASE("Move assignment removes what the target was already holding")
{
    g_removed.clear();
    FakeAdder first({1, 2});
    FakeAdder second({3, 4});
    auto a = VtableHook::Install("Test", true, true, [&](bool post) { return first(post); }, &RecordRemoval);
    auto b = VtableHook::Install("Test", true, true, [&](bool post) { return second(post); }, &RecordRemoval);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());

    *a = std::move(*b);

    REQUIRE(g_removed.size() == 2);
    CHECK(std::count(g_removed.begin(), g_removed.end(), 1) == 1);
    CHECK(std::count(g_removed.begin(), g_removed.end(), 2) == 1);
}

TEST_CASE("Install refuses a request with no adder, no remover, or no side")
{
    FakeAdder adder({5, 7});
    auto add = [&](bool post) { return adder(post); };

    CHECK(VtableHook::Install("Test", true, true, {}, &RecordRemoval).error().Code == ErrorCode::Invalid);
    CHECK(VtableHook::Install("Test", true, true, add, nullptr).error().Code == ErrorCode::Invalid);
    CHECK(VtableHook::Install("Test", false, false, add, &RecordRemoval).error().Code == ErrorCode::Invalid);
    CHECK(adder.Calls() == 0);
}

// The Lifecycle half of the contract: what a hook service's OnFirstSubscriber/OnLastSubscriber refcount has to do
// for the install to happen exactly once and the removal exactly once.

TEST_CASE("The first subscription installs and the last one to drop removes")
{
    int installs = 0;
    int removals = 0;
    int refs = 0;
    Event<int> event({.OnFirst =
                          [&] {
                              if (refs++ == 0)
                                  ++installs;
                              return true;
                          },
                      .OnLast =
                          [&] {
                              if (--refs == 0)
                                  ++removals;
                          }});

    {
        auto first = event += [](int) {};
        auto second = event += [](int) {};
        CHECK(installs == 1);
        CHECK(removals == 0);
    }

    CHECK(installs == 1);
    CHECK(removals == 1);
}

TEST_CASE("A refused install yields an empty subscription and never runs OnLast")
{
    int attempts = 0;
    int removals = 0;
    Event<int> event({.OnFirst =
                          [&] {
                              ++attempts;
                              return false;
                          },
                      .OnLast = [&] { ++removals; }});

    {
        auto refused = event += [](int) {};
        CHECK_FALSE(static_cast<bool>(refused));
        CHECK(event.Empty());
    }

    CHECK(attempts == 1);
    CHECK(removals == 0);
}

TEST_CASE("A refused install is retried by the next subscriber")
{
    bool ready = false;
    int installs = 0;
    Event<int> event({.OnFirst =
                          [&] {
                              if (!ready)
                                  return false;
                              ++installs;
                              return true;
                          },
                      .OnLast = [] {}});

    auto refused = event += [](int) {};
    CHECK_FALSE(static_cast<bool>(refused));

    ready = true;
    auto accepted = event += [](int) {};
    CHECK(static_cast<bool>(accepted));
    CHECK(installs == 1);
}
