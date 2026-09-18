#include <VoltMod/Core/Results/LoadSteps.hpp>
#include <doctest/doctest.h>
#include <string>
#include <utility>

using VoltMod::Error;
using VoltMod::LoadSteps;
using VoltMod::Status;

static Status Succeeds()
{
    return {};
}

static Status Fails(std::string reason)
{
    return std::unexpected(Error::Failed(std::move(reason)));
}

TEST_CASE("LoadSteps::Optional keeps loading and remembers the failure")
{
    LoadSteps steps;
    CHECK(steps.Optional("Database", [] { return Succeeds(); }));
    CHECK_FALSE(steps.Optional("Admins", [] { return Fails("no rows"); }));

    CHECK_EQ(steps.Count(), 2u);
    REQUIRE_EQ(steps.Failures().size(), 1u);
    CHECK_EQ(steps.Failures()[0].Name, "Admins");
    CHECK_EQ(steps.Failures()[0].Reason, "no rows");
    CHECK_FALSE(steps.Failures()[0].Required);
    CHECK(steps.AbortReason().empty());
}

TEST_CASE("LoadSteps::AbortReason names the first required step that failed")
{
    LoadSteps steps;
    steps.Optional("Database", [] { return Fails("offline"); });
    CHECK_FALSE(steps.Required("Configuration", [] { return Fails("settings.jsonc: unknown key"); }));
    steps.Required("SchemaLayout", [] { return Fails("drift"); });

    CHECK_EQ(steps.AbortReason(), "Configuration: settings.jsonc: unknown key");
}

TEST_CASE("LoadSteps::Summary lists only the steps that failed")
{
    LoadSteps steps;
    steps.Optional("Entities", [] { return Succeeds(); });
    CHECK(steps.Summary().ends_with(", none failed"));

    steps.Optional("Database", [] { return Fails("offline"); });
    const std::string summary = steps.Summary();
    CHECK(summary.starts_with("2 load steps in "));
    CHECK(summary.find("Entities") == std::string::npos);
    CHECK(summary.find("optional Database: offline") != std::string::npos);
}
