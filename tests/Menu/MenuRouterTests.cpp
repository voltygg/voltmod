#include "Menu/FakeMenuSurface.hpp"

#include <VoltMod/Menu/MenuRouter.hpp>
#include <doctest/doctest.h>
#include <memory>

using VoltMod::Menu;
using VoltMod::MenuRouter;
using VoltModTests::FakeMenuSurface;

static std::shared_ptr<Menu> AnyMenu()
{
    return std::make_shared<Menu>(Menu{.Title = "Admin"});
}

TEST_CASE("Without a preferred surface every session starts on the fallback")
{
    FakeMenuSurface fallback;
    MenuRouter router(fallback);

    CHECK(router.OpenSession(3, AnyMenu(), {}));
    CHECK(fallback.IsOpen(3));
    CHECK(router.IsOpen(3));
}

TEST_CASE("A session starts on the preferred surface and later calls follow it")
{
    FakeMenuSurface fallback;
    FakeMenuSurface preferred;
    MenuRouter router(fallback);
    const auto preference = router.Prefer(preferred);

    CHECK(router.OpenSession(3, AnyMenu(), {}));
    CHECK(preferred.SessionsOpened == 1);
    CHECK(fallback.SessionsOpened == 0);

    router.Open(3, AnyMenu());
    router.Close(3);
    CHECK(preferred.Opened.size() == 2);
    CHECK(preferred.Closes == 1);
    CHECK(fallback.Closes == 0);
}

TEST_CASE("A preferred surface that cannot draw for the player hands the session to the fallback")
{
    FakeMenuSurface fallback;
    FakeMenuSurface preferred;
    preferred.Accepts = false;
    MenuRouter router(fallback);
    const auto preference = router.Prefer(preferred);

    CHECK(router.OpenSession(3, AnyMenu(), {}));
    CHECK(fallback.SessionsOpened == 1);

    router.Close(3);
    CHECK(fallback.Closes == 1);
    CHECK(preferred.Closes == 0);
}

TEST_CASE("Opening a session closes the one the player has on the other surface")
{
    FakeMenuSurface fallback;
    FakeMenuSurface preferred;
    preferred.Accepts = false;
    MenuRouter router(fallback);
    const auto preference = router.Prefer(preferred);

    REQUIRE(router.OpenSession(3, AnyMenu(), {}));
    preferred.Accepts = true;
    REQUIRE(router.OpenSession(3, AnyMenu(), {}));

    CHECK_FALSE(fallback.IsOpen(3));
    CHECK(preferred.IsOpen(3));
}

TEST_CASE("Pushing a menu with no session open starts one")
{
    FakeMenuSurface fallback;
    MenuRouter router(fallback);

    router.Open(5, AnyMenu());
    CHECK(fallback.SessionsOpened == 1);
}

TEST_CASE("Dropping the preference sends new sessions to the fallback")
{
    FakeMenuSurface fallback;
    FakeMenuSurface preferred;
    MenuRouter router(fallback);

    {
        const auto preference = router.Prefer(preferred);
    }

    CHECK(router.OpenSession(3, AnyMenu(), {}));
    CHECK(preferred.SessionsOpened == 0);
    CHECK(fallback.SessionsOpened == 1);
}
