#include <VoltMod/Core/Result.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Error;
using VoltMod::ErrorCode;
using VoltMod::Result;
using VoltMod::Status;

TEST_CASE("A default-constructed Error is a failure, never a silent success")
{
    Error error;
    CHECK(error.Code == ErrorCode::Failed);
    CHECK(error.Detail.empty());
    CHECK(error.Key.empty());
}

TEST_CASE("Operator factories set the code and carry the detail")
{
    CHECK(Error::NotFound("no such player").Code == ErrorCode::NotFound);
    CHECK(Error::NotFound("no such player").Detail == "no such player");
    CHECK(Error::NotReady("database still connecting").Code == ErrorCode::NotReady);
    CHECK(Error::Invalid("duration does not parse").Code == ErrorCode::Invalid);
    CHECK(Error::Unsupported("gamedata has no 'RunCommand' index").Code == ErrorCode::Unsupported);
    CHECK(Error::Engine("SourceHook refused the hook").Code == ErrorCode::Engine);
    CHECK(Error::Failed("something else").Code == ErrorCode::Failed);

    CHECK(Error::Engine("SourceHook refused the hook").Key.empty());
}

TEST_CASE("The player-facing factories carry a translation key instead")
{
    const Error denied = Error::Denied("admin.no_permission");
    CHECK(denied.Code == ErrorCode::Denied);
    CHECK(denied.Key == "admin.no_permission");
    CHECK_FALSE(denied.Detail.empty());

    const Error immune = Error::Immune("admin.target_immune");
    CHECK(immune.Code == ErrorCode::Immune);
    CHECK(immune.Key == "admin.target_immune");
}

TEST_CASE("A Status carries either nothing or the reason it failed")
{
    Status ok{};
    CHECK(ok.has_value());

    Status failed = std::unexpected(Error::NotReady("interfaces not resolved"));
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().Code == ErrorCode::NotReady);
    CHECK(failed.error().Detail == "interfaces not resolved");
}

TEST_CASE("A Result holds the value on success and the error on failure")
{
    Result<int> value = 42;
    REQUIRE(value.has_value());
    CHECK(*value == 42);

    Result<int> missing = std::unexpected(Error::NotFound("slot 3 is empty"));
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().Code == ErrorCode::NotFound);
    CHECK(missing.value_or(-1) == -1);
}

TEST_CASE("A Result of a move-only payload keeps the payload intact")
{
    Result<std::string> name = std::string("bravo");
    REQUIRE(name.has_value());
    CHECK(*name == "bravo");
}
