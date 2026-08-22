#include <CS2Kit/Core/PluginManifest.hpp>
#include <doctest/doctest.h>

using CS2Kit::Core::IdentityKey;
using CS2Kit::Core::ParsePluginManifest;
using CS2Kit::Core::VersionAtLeast;

TEST_CASE("A manifest without dependencies parses to an empty list")
{
    const auto manifest = ParsePluginManifest(R"({"name": "bhop", "version": "1.2.0"})");
    REQUIRE(manifest.has_value());
    CHECK(manifest->Name == "bhop");
    CHECK(manifest->Version == "1.2.0");
    CHECK(manifest->Dependencies.empty());
}

TEST_CASE("Dependencies carry a minimum version and whether they are required")
{
    const auto manifest = ParsePluginManifest(R"({
        "name": "anticheat",
        "version": "1.0.0",
        "dependencies": [
            {"name": "admin-system", "minVersion": "1.0", "required": false},
            {"name": "core-thing", "required": true}
        ]})");
    REQUIRE(manifest.has_value());
    REQUIRE(manifest->Dependencies.size() == 2);
    CHECK(manifest->Dependencies[0].Name == "admin-system");
    CHECK(manifest->Dependencies[0].MinVersion == "1.0");
    CHECK_FALSE(manifest->Dependencies[0].Required);
    CHECK(manifest->Dependencies[1].MinVersion.empty());
    CHECK(manifest->Dependencies[1].Required);
}

TEST_CASE("A manifest that cannot say who it is describes nothing")
{
    CHECK_FALSE(ParsePluginManifest(R"({"version": "1.0.0"})").has_value());
    CHECK_FALSE(ParsePluginManifest(R"({"name": "", "version": "1.0.0"})").has_value());
}

TEST_CASE("Malformed manifests are rejected rather than silently half-read")
{
    CHECK_FALSE(ParsePluginManifest("not json at all").has_value());
    CHECK_FALSE(ParsePluginManifest(R"(["bhop"])").has_value());
    CHECK_FALSE(ParsePluginManifest(R"({"name": "a", "dependencies": {}})").has_value());
    CHECK_FALSE(ParsePluginManifest(R"({"name": "a", "dependencies": ["admin-system"]})").has_value());
    CHECK_FALSE(ParsePluginManifest(R"({"name": "a", "dependencies": [{"minVersion": "1.0"}]})").has_value());
}

TEST_CASE("A version satisfies an equal or lower requirement")
{
    CHECK(VersionAtLeast("1.2.0", "1.2.0"));
    CHECK(VersionAtLeast("1.2.1", "1.2.0"));
    CHECK(VersionAtLeast("2.0.0", "1.9.9"));
    CHECK_FALSE(VersionAtLeast("1.1.9", "1.2.0"));
    CHECK_FALSE(VersionAtLeast("0.9", "1.0"));
}

TEST_CASE("Missing version components read as zero")
{
    CHECK(VersionAtLeast("1.2", "1.2.0"));
    CHECK(VersionAtLeast("1", "1.0.0"));
    CHECK_FALSE(VersionAtLeast("1.2", "1.2.1"));
}

TEST_CASE("Ten beats nine rather than sorting before it")
{
    // The reason this compares numerically instead of lexicographically.
    CHECK(VersionAtLeast("1.10.0", "1.9.0"));
    CHECK_FALSE(VersionAtLeast("1.9.0", "1.10.0"));
}

TEST_CASE("A build or prerelease tail is ignored")
{
    CHECK(VersionAtLeast("1.2.0+a1b2c3d", "1.2.0"));
    CHECK(VersionAtLeast("1.2.0+a1b2c3d-dirty", "1.2.0"));
    CHECK_FALSE(VersionAtLeast("1.1.0+a1b2c3d", "1.2.0"));
}

TEST_CASE("An empty requirement accepts anything and an unparsable version accepts nothing")
{
    CHECK(VersionAtLeast("1.2.0", ""));
    CHECK(VersionAtLeast("whatever", ""));
    CHECK_FALSE(VersionAtLeast("", "1.0"));
    CHECK_FALSE(VersionAtLeast("unknown", "1.0"));
}

TEST_CASE("The identity key is namespaced by plugin name")
{
    CHECK(IdentityKey("admin-system") == "cs2kit.IPluginIdentity/1:admin-system");
    CHECK(IdentityKey("bhop") != IdentityKey("anticheat"));
}
