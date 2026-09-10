#include "FakeMenuSession.hpp"
#include "FakeTimers.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Menu/MenuStack.hpp>
#include <doctest/doctest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using VoltMod::Menu;
using VoltMod::MenuRow;
using VoltMod::MenuRowKind;
using VoltMod::MenuStack;
using VoltMod::SlotEvents;
using VoltMod::Translations;
using VoltModTests::FakeMenuSession;
using VoltModTests::FakeTimers;

static constexpr int kSlot = 0;

/** A row that describes itself as @p label and does nothing else. */
static VoltMod::MenuItem Line(std::string label)
{
    return {.Describe = [label](int) { return MenuRow{.Label = label}; }};
}

static std::shared_ptr<Menu> Screen(std::string title, std::vector<VoltMod::MenuItem> items = {})
{
    auto menu = std::make_shared<Menu>();
    menu->Title = std::move(title);
    menu->Items = std::move(items);
    return menu;
}

/** The subject and everything it borrows. Named for its subject: every test file is linked into
 *  one binary, so a bare `Fixture` here would collide with another file's. */
struct MenuStackFixture
{
    MenuStackFixture() : Stack(Session, Strings, Timers.Bind()) {}

    SlotEvents Slots;
    Translations Strings{Slots};
    FakeMenuSession Session;
    FakeTimers Timers;
    MenuStack Stack;
};

TEST_CASE("MenuStack: the breadcrumb names every menu below the top one")
{
    MenuStackFixture f;
    CHECK_FALSE(f.Stack.IsOpen(kSlot));

    f.Stack.Push(kSlot, Screen("Admin"));
    CHECK(f.Stack.IsOpen(kSlot));
    CHECK(f.Stack.Depth(kSlot) == 1);
    CHECK(f.Stack.Breadcrumb(kSlot).empty());  // the root is not below anything

    f.Stack.Push(kSlot, Screen("Players"));
    f.Stack.Push(kSlot, Screen("Ban"));
    CHECK(f.Stack.Depth(kSlot) == 3);
    CHECK(std::string(f.Stack.Breadcrumb(kSlot)) == "Admin › Players");
    CHECK(f.Stack.Root(kSlot)->Title == "Admin");
    CHECK(f.Stack.Current(kSlot)->Title == "Ban");
}

TEST_CASE("MenuStack: popping the last menu empties the session")
{
    MenuStackFixture f;
    f.Stack.Push(kSlot, Screen("Admin"));
    f.Stack.Push(kSlot, Screen("Players"));

    CHECK_FALSE(f.Stack.Pop(kSlot));  // a parent is showing again
    CHECK(f.Stack.Current(kSlot)->Title == "Admin");

    CHECK(f.Stack.Pop(kSlot));  // nothing left
    CHECK_FALSE(f.Stack.IsOpen(kSlot));
    CHECK(f.Stack.Current(kSlot) == nullptr);
}

TEST_CASE("MenuStack: Rewind unwinds to the root without closing the session")
{
    MenuStackFixture f;
    f.Stack.Push(kSlot, Screen("Admin"));
    f.Stack.Push(kSlot, Screen("Players"));
    f.Stack.Push(kSlot, Screen("Ban"));

    f.Stack.Rewind(kSlot);
    CHECK(f.Stack.Depth(kSlot) == 1);
    CHECK(f.Stack.IsOpen(kSlot));
    CHECK(f.Stack.Current(kSlot)->Title == "Admin");
    CHECK(f.Stack.Breadcrumb(kSlot).empty());
}

TEST_CASE("MenuStack: a toggle with no words of its own is given them")
{
    MenuStackFixture f;
    bool on = true;
    f.Stack.Push(kSlot, Screen("Admin", {{.Describe = [&on](int) {
                                             return MenuRow{.Kind = MenuRowKind::Toggle, .State = on};
                                         }}}));

    CHECK(f.Stack.Describe(kSlot, 0).Value == "ON");
    on = false;
    CHECK(f.Stack.Describe(kSlot, 0).Value == "OFF");
}

TEST_CASE("MenuStack: an index with no row behind it describes as an inert line")
{
    MenuStackFixture f;
    f.Stack.Push(kSlot, Screen("Admin", {Line("only")}));

    const MenuRow row = f.Stack.Describe(kSlot, 4);
    CHECK_FALSE(row.Enabled);
    CHECK_FALSE(row.Selectable);
}

TEST_CASE("MenuStack: a stepped row is marked pending until the commit runs")
{
    MenuStackFixture f;
    int value = 0;
    int commits = 0;
    f.Stack.Push(kSlot, Screen("Admin", {{
                                   .Describe = [&value](int) { return MenuRow{.Value = std::to_string(value)}; },
                                   .Step = [&value](int, int direction) { value += direction; return true; },
                                   .Commit = [&commits](int) { ++commits; },
                               }}));

    CHECK(f.Stack.Step(kSlot, 0, +1));
    CHECK(f.Stack.Step(kSlot, 0, +1));
    CHECK(value == 2);
    CHECK(commits == 0);  // held back: a burst of presses is one action
    CHECK(f.Stack.IsPending(kSlot, 0));
    CHECK(f.Stack.Describe(kSlot, 0).Pending);

    f.Timers.Elapse();
    CHECK(commits == 1);
    CHECK_FALSE(f.Stack.IsPending(kSlot, 0));
}

TEST_CASE("MenuStack: leaving the session applies what a stepped row was showing")
{
    MenuStackFixture f;
    int commits = 0;
    f.Stack.Push(kSlot, Screen("Admin", {{
                                   .Describe = [](int) { return MenuRow{}; },
                                   .Step = [](int, int) { return true; },
                                   .Commit = [&commits](int) { ++commits; },
                               }}));

    f.Stack.Step(kSlot, 0, +1);
    f.Stack.Clear(kSlot);
    CHECK(commits == 1);  // a value the player picked and watched appear is one they asked for
}

TEST_CASE("MenuStack: activating the pending row applies its value once, not twice")
{
    MenuStackFixture f;
    int commits = 0;
    int activations = 0;
    f.Stack.Push(kSlot, Screen("Admin", {{
                                   .Describe = [](int) { return MenuRow{}; },
                                   .Activate = [&](int, VoltMod::MenuSession&) { ++activations; ++commits; },
                                   .Step = [](int, int) { return true; },
                                   .Commit = [&commits](int) { ++commits; },
                               }}));

    f.Stack.Step(kSlot, 0, +1);
    f.Stack.Activate(kSlot, 0);

    CHECK(activations == 1);
    CHECK(commits == 1);  // the held commit was cancelled, not run alongside
    f.Timers.Elapse();
    CHECK(commits == 1);
}

TEST_CASE("MenuStack: a disabled row does not activate")
{
    MenuStackFixture f;
    int activations = 0;
    f.Stack.Push(kSlot, Screen("Admin", {{
                                   .Describe = [](int) { return MenuRow{.Enabled = false}; },
                                   .Activate = [&activations](int, VoltMod::MenuSession&) { ++activations; },
                               }}));

    f.Stack.Activate(kSlot, 0);
    CHECK(activations == 0);
}

TEST_CASE("MenuStack: a slot changing hands drops its session unrun")
{
    MenuStackFixture f;
    f.Stack.BindReset(f.Slots);

    int commits = 0;
    f.Stack.Push(kSlot, Screen("Admin", {{
                                   .Describe = [](int) { return MenuRow{}; },
                                   .Step = [](int, int) { return true; },
                                   .Commit = [&commits](int) { ++commits; },
                               }}));
    f.Stack.Step(kSlot, 0, +1);

    f.Slots.Raise(kSlot);
    CHECK_FALSE(f.Stack.IsOpen(kSlot));
    CHECK(commits == 0);  // nobody is left to have asked for it
}

