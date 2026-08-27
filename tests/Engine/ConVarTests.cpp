#include "Engine/ConVarTypes.hpp"

#include <VoltMod/Engine/ConVars.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::ConVarText;
using VoltMod::ConVarType;
using VoltMod::ConVarTypeMatches;

TEST_CASE("A convar handle only accepts the engine kind its C++ type describes")
{
    CHECK(ConVarTypeMatches<bool>(ConVarType::Bool));
    CHECK(ConVarTypeMatches<float>(ConVarType::Float32));
    CHECK(ConVarTypeMatches<std::string>(ConVarType::String));

    // The integer kinds that round-trip through int.
    CHECK(ConVarTypeMatches<int>(ConVarType::Int16));
    CHECK(ConVarTypeMatches<int>(ConVarType::UInt16));
    CHECK(ConVarTypeMatches<int>(ConVarType::Int32));
}

// A handle whose Find() succeeded used to narrow on every Get: UInt32/Int64/UInt64 were read
// through an int and Float64 through a float, so the typed promise was weaker than it looked.
TEST_CASE("A handle refuses an engine width it cannot represent")
{
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::UInt32));
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::Int64));
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::UInt64));
    CHECK_FALSE(ConVarTypeMatches<float>(ConVarType::Float64));
}

TEST_CASE("An int handle refuses a bool convar, which is the silent no-op this replaces")
{
    // sv_autobunnyhopping is a bool convar; SetInt on it used to change nothing at all.
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::Bool));
    CHECK_FALSE(ConVarTypeMatches<float>(ConVarType::Bool));
    CHECK_FALSE(ConVarTypeMatches<bool>(ConVarType::Int32));
    CHECK_FALSE(ConVarTypeMatches<bool>(ConVarType::Float32));
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::Float32));
    CHECK_FALSE(ConVarTypeMatches<float>(ConVarType::Int32));
    CHECK_FALSE(ConVarTypeMatches<std::string>(ConVarType::Int32));
    CHECK_FALSE(ConVarTypeMatches<int>(ConVarType::String));
}

TEST_CASE("No handle type matches a convar kind the framework does not model")
{
    for (auto kind : {ConVarType::Invalid, ConVarType::Color, ConVarType::Vector3, ConVarType::QAngle})
    {
        CHECK_FALSE(ConVarTypeMatches<bool>(kind));
        CHECK_FALSE(ConVarTypeMatches<int>(kind));
        CHECK_FALSE(ConVarTypeMatches<float>(kind));
        CHECK_FALSE(ConVarTypeMatches<std::string>(kind));
    }
}

TEST_CASE("ConVarText renders what a console line and a replicated message carry")
{
    // The client's own parser reads the replicated payload and does not accept "true"/"false".
    CHECK(ConVarText<bool>(true) == "1");
    CHECK(ConVarText<bool>(false) == "0");

    CHECK(ConVarText<int>(0) == "0");
    CHECK(ConVarText<int>(-12) == "-12");

    CHECK(ConVarText<float>(0.0f) == "0");
    CHECK(ConVarText<float>(0.5f) == "0.5");
    CHECK(ConVarText<float>(-80.0f) == "-80");

    CHECK(ConVarText<std::string>("de_dust2") == "de_dust2");
    CHECK(ConVarText<std::string>("") == "");
}
