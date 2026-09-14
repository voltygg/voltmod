#include "Ui/LayoutPath.hpp"

#include <doctest/doctest.h>
#include <ostream>

using VoltMod::ErrorCode;
using VoltMod::LayoutPath;

TEST_CASE("A bare name expands to the whitelisted directory")
{
    CHECK(LayoutPath::Parse("welcome").value().Resource() == "panorama/layout/custom_game/welcome.xml");
    CHECK(LayoutPath::Parse("welcome.xml").value().Resource() == "panorama/layout/custom_game/welcome.xml");
}

TEST_CASE("A full path under the whitelisted directory is kept as written")
{
    const auto path = "panorama/layout/custom_game/welcome.xml";
    CHECK(LayoutPath::Parse(path).value().Resource() == path);
}

TEST_CASE("The name is the file without its directory or extension")
{
    CHECK(LayoutPath::Parse("welcome").value().Name() == "welcome");
    CHECK(LayoutPath::Parse("panorama/layout/custom_game/admin_menu.xml").value().Name() == "admin_menu");
}

TEST_CASE("A layout outside the whitelisted directory is refused")
{
    const auto refused = LayoutPath::Parse("panorama/layout/hud/welcome.xml");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().Code == ErrorCode::Invalid);
}

TEST_CASE("A compiled resource name is refused, bare or with a path")
{
    // The client wants the source name and says so only on its own console.
    CHECK_FALSE(LayoutPath::Parse("welcome.vxml_c").has_value());
    CHECK_FALSE(LayoutPath::Parse("panorama/layout/custom_game/welcome.vxml_c").has_value());
}

TEST_CASE("An empty layout name is refused")
{
    CHECK_FALSE(LayoutPath::Parse("").has_value());
}

TEST_CASE("A dotted bare name is not silently turned into a path")
{
    CHECK_FALSE(LayoutPath::Parse("welcome.old").has_value());
}
