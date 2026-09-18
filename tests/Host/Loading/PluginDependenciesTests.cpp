#include "Host/Loading/PluginDependencies.hpp"

#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using VoltMod::ErrorCode;
using VoltMod::LoadList;
using VoltMod::PluginManifest;
using VoltMod::RefusedPlugin;
using VoltMod::PluginDependencies::Resolve;
using VoltMod::PluginDependencies::RequiredDependents;

static PluginManifest Plugin(std::string name, std::vector<std::string> dependencies = {},
                             std::vector<std::string> optionalDependencies = {})
{
    return {.Name = std::move(name),
            .Dependencies = std::move(dependencies),
            .OptionalDependencies = std::move(optionalDependencies)};
}

static const RefusedPlugin* Refusal(const LoadList& list, std::string_view name)
{
    for (const RefusedPlugin& refused : list.Refused)
        if (refused.Name == name)
            return &refused;

    return nullptr;
}

TEST_CASE("PluginDependencies::Resolve loads everything whose dependencies are installed, alphabetically")
{
    const std::vector<PluginManifest> installed{Plugin("zulu", {"alpha"}), Plugin("alpha"), Plugin("mike")};

    const LoadList list = Resolve(installed);

    CHECK(list.Refused.empty());
    CHECK_EQ(list.Allowed, std::vector<std::string>{"alpha", "mike", "zulu"});
}

TEST_CASE("PluginDependencies::Resolve ignores an optional dependency that is not installed")
{
    const std::vector<PluginManifest> installed{Plugin("anticheat", {}, {"admin-system"})};

    const LoadList list = Resolve(installed);

    CHECK(list.Refused.empty());
    CHECK_EQ(list.Allowed, std::vector<std::string>{"anticheat"});
}

TEST_CASE("PluginDependencies::Resolve loads two plugins that require each other")
{
    // Nothing is ordered, so requiring each other is only a statement that both must be installed.
    const std::vector<PluginManifest> installed{Plugin("a", {"b"}), Plugin("b", {"a"})};

    const LoadList list = Resolve(installed);

    CHECK(list.Refused.empty());
    CHECK_EQ(list.Allowed, std::vector<std::string>{"a", "b"});
}

TEST_CASE("PluginDependencies::Resolve refuses a plugin whose required dependency is missing")
{
    const std::vector<PluginManifest> installed{Plugin("reports", {"admin-system"}), Plugin("bhop")};

    const LoadList list = Resolve(installed);

    CHECK_EQ(list.Allowed, std::vector<std::string>{"bhop"});
    REQUIRE_EQ(list.Refused.size(), 1u);
    CHECK_EQ(list.Refused[0].Name, "reports");
    CHECK_EQ(list.Refused[0].Reason.Code, ErrorCode::NotFound);
    CHECK_EQ(list.Refused[0].Reason.Detail, "requires 'admin-system', which is not installed");
}

TEST_CASE("PluginDependencies::Resolve refuses what a refused plugin was needed for")
{
    const std::vector<PluginManifest> installed{Plugin("menus", {"reports"}), Plugin("reports", {"admin-system"})};

    const LoadList list = Resolve(installed);

    CHECK(list.Allowed.empty());
    REQUIRE_EQ(list.Refused.size(), 2u);

    const RefusedPlugin* menus = Refusal(list, "menus");
    REQUIRE(menus != nullptr);
    CHECK_EQ(menus->Reason.Code, ErrorCode::NotReady);
    CHECK_EQ(menus->Reason.Detail, "requires 'reports', which the host refused");
    CHECK_EQ(Refusal(list, "reports")->Reason.Detail, "requires 'admin-system', which is not installed");
}

TEST_CASE("PluginDependencies::Resolve keeps an optional dependent of a refused plugin")
{
    const std::vector<PluginManifest> installed{Plugin("anticheat", {}, {"reports"}), Plugin("reports", {"missing"})};

    const LoadList list = Resolve(installed);

    CHECK_EQ(list.Allowed, std::vector<std::string>{"anticheat"});
    REQUIRE_EQ(list.Refused.size(), 1u);
    CHECK_EQ(list.Refused[0].Name, "reports");
}

TEST_CASE("PluginDependencies::Resolve refuses a plugin name that is installed twice")
{
    const std::vector<PluginManifest> installed{Plugin("bhop"), Plugin("bhop")};

    const LoadList list = Resolve(installed);

    CHECK(list.Allowed.empty());
    REQUIRE_EQ(list.Refused.size(), 1u);
    CHECK_EQ(list.Refused[0].Reason.Code, ErrorCode::Invalid);
}

TEST_CASE("PluginDependencies::Resolve refuses what a plugin installed twice was needed for")
{
    const std::vector<PluginManifest> installed{Plugin("bhop"), Plugin("bhop"), Plugin("stats", {"bhop"})};

    const LoadList list = Resolve(installed);

    CHECK(list.Allowed.empty());
    REQUIRE_EQ(list.Refused.size(), 2u);
    CHECK_EQ(Refusal(list, "stats")->Reason.Detail, "requires 'bhop', which the host refused");
}

TEST_CASE("PluginDependencies::RequiredDependents finds dependents through another plugin")
{
    const std::vector<PluginManifest> loaded{Plugin("admin-system"), Plugin("reports", {"admin-system"}),
                                             Plugin("menus", {"reports"}), Plugin("bhop")};

    CHECK_EQ(RequiredDependents("admin-system", loaded), std::vector<std::string>{"menus", "reports"});
    CHECK_EQ(RequiredDependents("reports", loaded), std::vector<std::string>{"menus"});
    CHECK(RequiredDependents("menus", loaded).empty());
    CHECK(RequiredDependents("bhop", loaded).empty());
}

TEST_CASE("PluginDependencies::RequiredDependents ignores optional dependents")
{
    const std::vector<PluginManifest> loaded{Plugin("admin-system"), Plugin("anticheat", {}, {"admin-system"})};

    CHECK(RequiredDependents("admin-system", loaded).empty());
}
