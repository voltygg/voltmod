#include "Host/Loading/InstalledPlugins.hpp"
#include "Support/TempPath.hpp"

#include <VoltMod/Host/Abi.hpp>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

using VoltMod::PluginDescriptor;
using VoltMod::PluginManifest;
using VoltMod::ValidateDescriptor;
using VoltMod::InstalledPlugins::Discover;

/** An installed plugins directory the case fills with named plugin directories. */
class Plugins
{
public:
    explicit Plugins(std::string_view tag) : _root(tag) {}

    /** Create `<plugins>/<directory>`, holding @p manifest as plugin.json unless it is empty. */
    void Install(std::string_view directory, std::string_view manifest) const
    {
        const std::filesystem::path home = std::filesystem::path(_root.Path()) / directory;
        std::filesystem::create_directories(home);
        if (!manifest.empty())
        {
            std::ofstream out(home / "plugin.json", std::ios::binary);
            out << manifest;
        }
    }

    std::filesystem::path Path() const { return _root.Path(); }

private:
    VoltModTests::TempDir _root;
};

static bool LoadPlugin(VoltMod::IHost*, char*, size_t)
{
    return true;
}

static void UnloadPlugin() {}

static const char* PluginStatus()
{
    return "{}";
}

static PluginDescriptor Descriptor()
{
    return {.AbiVersion = VoltMod::HostAbiVersion,
            .Load = LoadPlugin,
            .Unload = UnloadPlugin,
            .Status = PluginStatus};
}

TEST_CASE("A manifest is read with its optional fields defaulted")
{
    const Plugins plugins("installed-plugins");
    plugins.Install("bhop", R"({ "name": "bhop", "version": "1.2.0", "dependencies": ["admin-system"] })");

    const std::vector<PluginManifest> installed = Discover(plugins.Path());

    REQUIRE(installed.size() == 1u);
    CHECK(installed[0].Name == "bhop");
    CHECK(installed[0].Version == "1.2.0");
    CHECK(installed[0].LogTag == "bhop");
    CHECK(installed[0].Description.empty());
    CHECK(installed[0].Dependencies == std::vector<std::string>{"admin-system"});
    CHECK(installed[0].OptionalDependencies.empty());
}

TEST_CASE("A log tag the manifest gives is kept")
{
    const Plugins plugins("installed-plugins");
    plugins.Install("admin-system", R"({ "name": "admin-system", "version": "2.0.0", "logTag": "Admin" })");

    const std::vector<PluginManifest> installed = Discover(plugins.Path());

    REQUIRE(installed.size() == 1u);
    CHECK(installed[0].LogTag == "Admin");
}

TEST_CASE("A manifest naming a plugin other than its directory is refused")
{
    const Plugins plugins("installed-plugins");
    plugins.Install("bhop", R"({ "name": "surf", "version": "1.0.0" })");

    CHECK(Discover(plugins.Path()).empty());
}

TEST_CASE("A malformed manifest, or one with a misspelled key, is refused")
{
    const Plugins plugins("installed-plugins");
    plugins.Install("bhop", R"({ "name": "bhop", )");
    plugins.Install("surf", R"({ "name": "surf", "version": "1.0.0", "dependancies": ["bhop"] })");

    CHECK(Discover(plugins.Path()).empty());
}

TEST_CASE("A directory without a manifest is not a plugin")
{
    const Plugins plugins("installed-plugins");
    plugins.Install("not-a-plugin", "");
    plugins.Install("bhop", R"({ "name": "bhop", "version": "1.0.0" })");

    const std::vector<PluginManifest> installed = Discover(plugins.Path());

    REQUIRE(installed.size() == 1u);
    CHECK(installed[0].Name == "bhop");
}

TEST_CASE("A plugins directory that is not there resolves to nothing")
{
    CHECK(Discover("voltmod-no-such-plugins-directory").empty());
}

TEST_CASE("A descriptor is accepted only whole and only at this host's ABI version")
{
    const PluginDescriptor good = Descriptor();
    CHECK(ValidateDescriptor(&good).has_value());
    CHECK_FALSE(ValidateDescriptor(nullptr).has_value());

    PluginDescriptor other = Descriptor();
    other.AbiVersion = VoltMod::HostAbiVersion + 1;
    CHECK_FALSE(ValidateDescriptor(&other).has_value());

    PluginDescriptor noLoad = Descriptor();
    noLoad.Load = nullptr;
    CHECK_FALSE(ValidateDescriptor(&noLoad).has_value());

    PluginDescriptor noUnload = Descriptor();
    noUnload.Unload = nullptr;
    CHECK_FALSE(ValidateDescriptor(&noUnload).has_value());

    PluginDescriptor noStatus = Descriptor();
    noStatus.Status = nullptr;
    CHECK_FALSE(ValidateDescriptor(&noStatus).has_value());
}
