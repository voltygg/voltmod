#include <VoltMod/App/PluginSettings.hpp>
#include <VoltMod/Core/Result.hpp>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

// LoadStandardConfig picks its behaviour with `if constexpr` on these concepts. That is invisible
// at runtime: before they were named, a settings root that quietly lost its `plugin` section just
// stopped having its locale applied, with nothing said. These cases are the guard for that.

using VoltMod::HasLoadSettings;
using VoltMod::HasPluginSection;
using VoltMod::StandardPluginSettings;

/** The admin-system / bhop shape: a root carrying the standard plugin section. */
struct RootWithPlugin
{
    StandardPluginSettings plugin;
    int other = 0;
};

/** The anticheat shape: a root with no plugin section at all. */
struct RootWithoutPlugin
{
    int anticheat = 0;
};

/** A root whose `plugin` is something else entirely; it must not satisfy the concept. */
struct RootWithForeignPlugin
{
    std::string plugin;
};

struct LoadsAndValidates
{
    VoltMod::Status LoadSettings(std::string_view) { return {}; }
};

struct LoadsOnly
{
    VoltMod::Status Load(std::string_view) { return {}; }
};

TEST_CASE("HasPluginSection recognizes a root carrying the standard plugin section")
{
    static_assert(HasPluginSection<RootWithPlugin>);
    CHECK(HasPluginSection<RootWithPlugin>);
}

TEST_CASE("HasPluginSection rejects a root with no plugin section")
{
    // This is the live case: anticheat's settings root has only `anticheat`, so its locale was
    // never applied and nothing reported it.
    static_assert(!HasPluginSection<RootWithoutPlugin>);
    CHECK_FALSE(HasPluginSection<RootWithoutPlugin>);
}

TEST_CASE("HasPluginSection rejects a plugin member of an unrelated type")
{
    static_assert(!HasPluginSection<RootWithForeignPlugin>);
    CHECK_FALSE(HasPluginSection<RootWithForeignPlugin>);
}

TEST_CASE("HasLoadSettings picks the load-then-validate convention only when it exists")
{
    static_assert(HasLoadSettings<LoadsAndValidates>);
    static_assert(!HasLoadSettings<LoadsOnly>);
    CHECK(HasLoadSettings<LoadsAndValidates>);
    CHECK_FALSE(HasLoadSettings<LoadsOnly>);
}
