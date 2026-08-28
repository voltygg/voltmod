#include "Ui/UiClickRouting.hpp"

#include <doctest/doctest.h>
#include <functional>
#include <string>
#include <vector>

using VoltMod::EntityRef;
using VoltMod::Event;
using VoltMod::Subscription;
using VoltMod::UiClick;
using VoltMod::Internal::RouteUiClick;
using VoltMod::Internal::UiButtonEvents;

/** The pieces a UiPanel hands the router, with no entity system in sight. */
struct RoutingFixture
{
    EntityRef Layout{.Handle = 7};
    Event<const UiClick&> Clicked;
    UiButtonEvents Buttons;

    /** Subscribe to one button id the way UiPanel::Button does. */
    Subscription OnButton(const std::string& id, std::function<void(int)> handler)
    {
        return Buttons.try_emplace(id).first->second += std::move(handler);
    }

    bool Route(const UiClick& click) { return RouteUiClick(click, Layout, Clicked, Buttons); }
};

TEST_CASE("A press raises the panel event and the button it names")
{
    RoutingFixture panel;
    std::vector<std::string> seen;
    int pressedBy = -1;

    const Subscription all = panel.Clicked += [&](const UiClick& click) { seen.push_back(click.ButtonId); };
    const Subscription accept = panel.OnButton("accept", [&](int slot) { pressedBy = slot; });

    CHECK(panel.Route(UiClick{.Slot = 3, .Layout = EntityRef{.Handle = 7}, .ButtonId = "accept"}));

    CHECK(seen == std::vector<std::string>{"accept"});
    CHECK(pressedBy == 3);
}

TEST_CASE("A press in another layout is not this panel's")
{
    RoutingFixture panel;
    bool anything = false;

    const Subscription all = panel.Clicked += [&](const UiClick&) { anything = true; };
    const Subscription accept = panel.OnButton("accept", [&](int) { anything = true; });

    CHECK_FALSE(panel.Route(UiClick{.Slot = 3, .Layout = EntityRef{.Handle = 9}, .ButtonId = "accept"}));
    CHECK_FALSE(anything);
}

TEST_CASE("A press before the panel has spawned matches nothing")
{
    RoutingFixture panel;
    panel.Layout = {};
    bool anything = false;

    const Subscription all = panel.Clicked += [&](const UiClick&) { anything = true; };

    CHECK_FALSE(panel.Route(UiClick{.Slot = 3, .Layout = EntityRef{}, .ButtonId = "accept"}));
    CHECK_FALSE(anything);
}

TEST_CASE("A button id nobody subscribed to still raises the panel event")
{
    RoutingFixture panel;
    int panelEvents = 0;
    int acceptPresses = 0;

    const Subscription all = panel.Clicked += [&](const UiClick&) { ++panelEvents; };
    const Subscription accept = panel.OnButton("accept", [&](int) { ++acceptPresses; });

    CHECK(panel.Route(UiClick{.Slot = 3, .Layout = EntityRef{.Handle = 7}, .ButtonId = "decline"}));

    CHECK(panelEvents == 1);
    CHECK(acceptPresses == 0);
}

TEST_CASE("A button subscription taken before any press still fires")
{
    RoutingFixture panel;
    std::vector<int> slots;

    const Subscription accept = panel.OnButton("accept", [&](int slot) { slots.push_back(slot); });

    CHECK(panel.Route(UiClick{.Slot = 1, .Layout = EntityRef{.Handle = 7}, .ButtonId = "accept"}));
    CHECK(panel.Route(UiClick{.Slot = 2, .Layout = EntityRef{.Handle = 7}, .ButtonId = "accept"}));

    CHECK(slots == std::vector<int>{1, 2});
}

TEST_CASE("Dropping a button subscription stops its handler")
{
    RoutingFixture panel;
    int presses = 0;

    {
        const Subscription accept = panel.OnButton("accept", [&](int) { ++presses; });
        CHECK(panel.Route(UiClick{.Slot = 1, .Layout = EntityRef{.Handle = 7}, .ButtonId = "accept"}));
    }

    CHECK(panel.Route(UiClick{.Slot = 1, .Layout = EntityRef{.Handle = 7}, .ButtonId = "accept"}));
    CHECK(presses == 1);
}
