#include <VoltMod/Core/Paths.hpp>
#include <doctest/doctest.h>
#include <filesystem>

using namespace VoltMod::Core;

TEST_CASE("AddonDir builds the engine-relative addon root")
{
    CHECK(AddonDir("bhop") == "addons/bhop");
    CHECK(AddonDir("admin-system") == "addons/admin-system");
}

TEST_CASE("AddonFile joins the addon root with a relative path")
{
    CHECK(AddonFile("bhop", "configs/settings.jsonc") == "addons/bhop/configs/settings.jsonc");
    CHECK(AddonFile("anticheat", "configs/translations") == "addons/anticheat/configs/translations");
}

TEST_CASE("ResolvePath joins relative paths against the base dir and passes absolute paths through")
{
    SetBaseDir("/srv/cs2");
    CHECK(ResolvePath("addons/bhop") == std::filesystem::path("/srv/cs2") / "addons/bhop");

    const auto absolute = std::filesystem::current_path();
    CHECK(ResolvePath(absolute.string()) == absolute);

    SetBaseDir("");
}
