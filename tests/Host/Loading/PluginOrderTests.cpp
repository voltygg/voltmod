#include "Host/Loading/PluginOrder.hpp"

#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using VoltMod::ErrorCode;
using VoltMod::LoadPlan;
using VoltMod::PluginManifest;
using VoltMod::RefusedPlugin;
using VoltMod::PluginOrder::Plan;
using VoltMod::PluginOrder::ReloadGroup;
using VoltMod::PluginOrder::RequiredDependents;

static PluginManifest Plugin(std::string name, std::vector<std::string> dependencies = {},
                             std::vector<std::string> optionalDependencies = {})
{
    return {.Name = std::move(name),
            .Dependencies = std::move(dependencies),
            .OptionalDependencies = std::move(optionalDependencies)};
}

static const RefusedPlugin* Refusal(const LoadPlan& plan, std::string_view name)
{
    for (const RefusedPlugin& refused : plan.Refused)
        if (refused.Name == name)
            return &refused;

    return nullptr;
}

TEST_CASE("PluginOrder::Plan loads a chain in dependency order")
{
    const std::vector<PluginManifest> installed{Plugin("c", {"b"}), Plugin("b", {"a"}), Plugin("a")};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Refused.empty());
    CHECK_EQ(plan.Order, std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("PluginOrder::Plan breaks ties alphabetically")
{
    const std::vector<PluginManifest> installed{Plugin("zulu"), Plugin("alpha"), Plugin("mike")};

    CHECK_EQ(Plan(installed).Order, std::vector<std::string>{"alpha", "mike", "zulu"});
}

TEST_CASE("PluginOrder::Plan orders an optional dependency that is installed")
{
    // anticheat sorts first, so only the optional edge can put admin-system ahead of it.
    const std::vector<PluginManifest> installed{Plugin("anticheat", {}, {"admin-system"}), Plugin("admin-system")};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Refused.empty());
    CHECK_EQ(plan.Order, std::vector<std::string>{"admin-system", "anticheat"});
}

TEST_CASE("PluginOrder::Plan ignores an optional dependency that is not installed")
{
    const std::vector<PluginManifest> installed{Plugin("anticheat", {}, {"admin-system"})};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Refused.empty());
    CHECK_EQ(plan.Order, std::vector<std::string>{"anticheat"});
}

TEST_CASE("PluginOrder::Plan refuses a plugin whose required dependency is missing")
{
    const std::vector<PluginManifest> installed{Plugin("reports", {"admin-system"}), Plugin("bhop")};

    const LoadPlan plan = Plan(installed);

    CHECK_EQ(plan.Order, std::vector<std::string>{"bhop"});
    REQUIRE_EQ(plan.Refused.size(), 1u);
    CHECK_EQ(plan.Refused[0].Name, "reports");
    CHECK_EQ(plan.Refused[0].Reason.Code, ErrorCode::NotFound);
    CHECK_EQ(plan.Refused[0].Reason.Detail, "requires 'admin-system', which is not installed");
}

TEST_CASE("PluginOrder::Plan refuses what a refused plugin was needed for")
{
    const std::vector<PluginManifest> installed{Plugin("menus", {"reports"}), Plugin("reports", {"admin-system"})};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Order.empty());
    REQUIRE_EQ(plan.Refused.size(), 2u);

    const RefusedPlugin* menus = Refusal(plan, "menus");
    REQUIRE(menus != nullptr);
    CHECK_EQ(menus->Reason.Code, ErrorCode::NotReady);
    CHECK_EQ(menus->Reason.Detail, "requires 'reports', which the host refused");
    CHECK_EQ(Refusal(plan, "reports")->Reason.Detail, "requires 'admin-system', which is not installed");
}

TEST_CASE("PluginOrder::Plan keeps an optional dependent of a refused plugin")
{
    const std::vector<PluginManifest> installed{Plugin("anticheat", {}, {"reports"}), Plugin("reports", {"missing"})};

    const LoadPlan plan = Plan(installed);

    CHECK_EQ(plan.Order, std::vector<std::string>{"anticheat"});
    REQUIRE_EQ(plan.Refused.size(), 1u);
    CHECK_EQ(plan.Refused[0].Name, "reports");
}

TEST_CASE("PluginOrder::Plan refuses both plugins in a two-node cycle")
{
    const std::vector<PluginManifest> installed{Plugin("a", {"b"}), Plugin("b", {"a"})};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Order.empty());
    REQUIRE_EQ(plan.Refused.size(), 2u);
    CHECK_EQ(plan.Refused[0].Reason.Code, ErrorCode::Invalid);
    CHECK_EQ(plan.Refused[0].Reason.Detail, "dependency cycle: a -> b -> a");
    CHECK_EQ(plan.Refused[1].Reason.Detail, "dependency cycle: a -> b -> a");
}

TEST_CASE("PluginOrder::Plan refuses every plugin in a three-node cycle")
{
    const std::vector<PluginManifest> installed{Plugin("a", {"b"}), Plugin("b", {"c"}), Plugin("c", {"a"})};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Order.empty());
    REQUIRE_EQ(plan.Refused.size(), 3u);
    CHECK_EQ(plan.Refused[0].Name, "a");
    CHECK_EQ(plan.Refused[1].Name, "b");
    CHECK_EQ(plan.Refused[2].Name, "c");
    CHECK_EQ(plan.Refused[0].Reason.Detail, "dependency cycle: a -> b -> c -> a");
}

TEST_CASE("PluginOrder::Plan refuses a cycle made of optional dependencies")
{
    const std::vector<PluginManifest> installed{Plugin("a", {}, {"b"}), Plugin("b", {}, {"a"})};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Order.empty());
    CHECK_EQ(plan.Refused.size(), 2u);
}

TEST_CASE("PluginOrder::Plan leaves plugins outside a cycle alone")
{
    const std::vector<PluginManifest> installed{Plugin("a", {"b"}), Plugin("b", {"a"}), Plugin("bhop"),
                                                Plugin("stats", {"bhop"})};

    const LoadPlan plan = Plan(installed);

    CHECK_EQ(plan.Order, std::vector<std::string>{"bhop", "stats"});
    REQUIRE_EQ(plan.Refused.size(), 2u);
    CHECK_EQ(plan.Refused[0].Name, "a");
    CHECK_EQ(plan.Refused[1].Name, "b");
}

TEST_CASE("PluginOrder::Plan refuses a plugin name that is installed twice")
{
    const std::vector<PluginManifest> installed{Plugin("bhop"), Plugin("bhop")};

    const LoadPlan plan = Plan(installed);

    CHECK(plan.Order.empty());
    REQUIRE_EQ(plan.Refused.size(), 1u);
    CHECK_EQ(plan.Refused[0].Reason.Code, ErrorCode::Invalid);
}

TEST_CASE("PluginOrder::RequiredDependents finds dependents through another plugin")
{
    const std::vector<PluginManifest> loaded{Plugin("admin-system"), Plugin("reports", {"admin-system"}),
                                             Plugin("menus", {"reports"}), Plugin("bhop")};

    CHECK_EQ(RequiredDependents("admin-system", loaded), std::vector<std::string>{"menus", "reports"});
    CHECK_EQ(RequiredDependents("reports", loaded), std::vector<std::string>{"menus"});
    CHECK(RequiredDependents("menus", loaded).empty());
    CHECK(RequiredDependents("bhop", loaded).empty());
}

TEST_CASE("PluginOrder::RequiredDependents ignores optional dependents")
{
    const std::vector<PluginManifest> loaded{Plugin("admin-system"), Plugin("anticheat", {}, {"admin-system"})};

    CHECK(RequiredDependents("admin-system", loaded).empty());
}

TEST_CASE("PluginOrder::ReloadGroup takes the dependents down with the plugin, newest first")
{
    const std::vector<PluginManifest> loaded{Plugin("admin-system"), Plugin("reports", {"admin-system"}),
                                             Plugin("menus", {"reports"}), Plugin("bhop")};

    CHECK_EQ(ReloadGroup("admin-system", loaded), std::vector<std::string>{"menus", "reports", "admin-system"});
    CHECK_EQ(ReloadGroup("bhop", loaded), std::vector<std::string>{"bhop"});
}
