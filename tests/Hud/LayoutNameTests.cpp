#include "Hud/LayoutName.hpp"

#include <doctest/doctest.h>

using VoltMod::ErrorCode;
using VoltMod::ResolveLayoutName;

TEST_CASE("A bare name expands to the whitelisted directory")
{
    CHECK(ResolveLayoutName("welcome").value() == "panorama/layout/custom_game/welcome.xml");
    CHECK(ResolveLayoutName("welcome.xml").value() == "panorama/layout/custom_game/welcome.xml");
}

TEST_CASE("A full path under the whitelisted directory is kept as written")
{
    const auto path = "panorama/layout/custom_game/welcome.xml";
    CHECK(ResolveLayoutName(path).value() == path);
}

TEST_CASE("A layout outside the whitelisted directory is refused")
{
    const auto refused = ResolveLayoutName("panorama/layout/hud/welcome.xml");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().Code == ErrorCode::Invalid);
}

TEST_CASE("A compiled resource name is refused, bare or with a path")
{
    // The client wants the source name even though what ships is compiled, and it says so only on
    // its own console - so both spellings have to fail here instead.
    CHECK_FALSE(ResolveLayoutName("welcome.vxml_c").has_value());
    CHECK_FALSE(ResolveLayoutName("panorama/layout/custom_game/welcome.vxml_c").has_value());
}

TEST_CASE("An empty layout name is refused")
{
    CHECK_FALSE(ResolveLayoutName("").has_value());
}

TEST_CASE("A dotted bare name is not silently turned into a path")
{
    // "welcome.old" would otherwise become welcome.old.xml, which resolves to nothing.
    CHECK_FALSE(ResolveLayoutName("welcome.old").has_value());
}
