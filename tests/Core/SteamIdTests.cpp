#include <VoltMod/Core/SteamId.hpp>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>

using VoltMod::SteamId;

// Reference identity: account id 22202.
static constexpr int64_t Base = 76561197960265728LL;
static constexpr int64_t SampleId64 = Base + 22202;  // 76561197960287930

TEST_CASE("SteamId::IsValid range")
{
    CHECK(SteamId::IsValid(Base));
    CHECK(SteamId::IsValid(SampleId64));
    CHECK(!SteamId::IsValid(Base - 1));
    CHECK(!SteamId::IsValid(0));
    CHECK(!SteamId::IsValid(Base + 0x100000000LL));  // one past the top
    CHECK(SteamId::IsValid(Base + 0xFFFFFFFFLL));    // top valid account id
}

TEST_CASE("SteamId::GetAccountId")
{
    CHECK_EQ(SteamId::GetAccountId(SampleId64), static_cast<uint32_t>(22202));
    CHECK_EQ(SteamId::GetAccountId(Base), static_cast<uint32_t>(0));
}

TEST_CASE("SteamId::ToSteamId3")
{
    CHECK_EQ(SteamId::ToSteamId3(SampleId64), std::string("[U:1:22202]"));
    CHECK_EQ(SteamId::ToSteamId3(Base), std::string("[U:1:0]"));
}

TEST_CASE("SteamId::ToSteamId")
{
    // 22202 -> authServer 0, accountNum 11101
    CHECK_EQ(SteamId::ToSteamId(SampleId64), std::string("STEAM_0:0:11101"));
}

TEST_CASE("SteamId::FromSteamId3 valid")
{
    auto id = SteamId::FromSteamId3("[U:1:22202]");
    REQUIRE(id.has_value());
    CHECK_EQ(*id, SampleId64);
}

TEST_CASE("SteamId::FromSteamId3 invalid")
{
    CHECK(!SteamId::FromSteamId3("U:1:22202").has_value());  // missing brackets
    CHECK(!SteamId::FromSteamId3("[U:1:]").has_value());     // no digits
    CHECK(!SteamId::FromSteamId3("[U:1:abc]").has_value());  // non-numeric
    CHECK(!SteamId::FromSteamId3("garbage").has_value());
}

TEST_CASE("SteamId::FromSteamId valid")
{
    auto id = SteamId::FromSteamId("STEAM_0:0:11101");
    REQUIRE(id.has_value());
    CHECK_EQ(*id, SampleId64);

    // Odd account id via authServer bit.
    auto id1 = SteamId::FromSteamId("STEAM_0:1:11100");
    REQUIRE(id1.has_value());
    CHECK_EQ(*id1, Base + (11100 * 2 + 1));
}

TEST_CASE("SteamId::FromSteamId invalid")
{
    CHECK(!SteamId::FromSteamId("STEAM_0:2:11101").has_value());  // authServer out of [0,1]
    CHECK(!SteamId::FromSteamId("STEAM_6:0:11101").has_value());  // universe out of [0,5]
    CHECK(!SteamId::FromSteamId("STEAM_0:0:").has_value());       // no account number
    CHECK(!SteamId::FromSteamId("not a steam id").has_value());
}

TEST_CASE("SteamId round-trip 64 -> 3 -> 64")
{
    auto back = SteamId::FromSteamId3(SteamId::ToSteamId3(SampleId64));
    REQUIRE(back.has_value());
    CHECK_EQ(*back, SampleId64);
}

TEST_CASE("SteamId round-trip 64 -> 2 -> 64")
{
    auto back = SteamId::FromSteamId(SteamId::ToSteamId(SampleId64));
    REQUIRE(back.has_value());
    CHECK_EQ(*back, SampleId64);
}
