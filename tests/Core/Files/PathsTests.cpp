#include <VoltMod/Core/Files/Paths.hpp>
#include <doctest/doctest.h>
#include <filesystem>

using VoltMod::PluginDir;
using VoltMod::PluginFile;
using VoltMod::ResolvePath;
using VoltMod::SetBaseDir;

TEST_CASE("PluginDir builds the engine-relative plugin root")
{
    CHECK(PluginDir("bhop") == "addons/voltmod/plugins/bhop");
    CHECK(PluginDir("admin-system") == "addons/voltmod/plugins/admin-system");
}

TEST_CASE("PluginFile joins the plugin root with a relative path")
{
    CHECK(PluginFile("bhop", "configs/settings.jsonc") == "addons/voltmod/plugins/bhop/configs/settings.jsonc");
    CHECK(PluginFile("anticheat", "translations") == "addons/voltmod/plugins/anticheat/translations");
}

TEST_CASE("ResolvePath joins relative paths against the base dir and passes absolute paths through")
{
    SetBaseDir("/srv/cs2");
    CHECK(ResolvePath("addons/voltmod/plugins/bhop") ==
          std::filesystem::path("/srv/cs2") / "addons/voltmod/plugins/bhop");

    const auto absolute = std::filesystem::current_path();
    CHECK(ResolvePath(absolute.string()) == absolute);

    SetBaseDir("");
}
