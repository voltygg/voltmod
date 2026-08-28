#include "Ui/ClickPayload.hpp"

#include <doctest/doctest.h>
#include <string>

using VoltMod::ErrorCode;
using VoltMod::ParseClickPayload;

/** A protobuf key byte: field number in the high bits, wire type in the low three. */
static char Key(int field, int wireType)
{
    return static_cast<char>((field << 3) | wireType);
}

/** field 1 (varint) then field 2 (length-delimited), the order a press arrives in. */
static std::string Press(unsigned char layout, const std::string& button)
{
    std::string bytes;
    bytes += Key(1, 0);
    bytes += static_cast<char>(layout);
    bytes += Key(2, 2);
    bytes += static_cast<char>(button.size());
    bytes += button;
    return bytes;
}

TEST_CASE("A press carries the layout handle and the button id")
{
    auto parsed = ParseClickPayload(Press(7, "vm_row3"));
    REQUIRE(parsed.has_value());
    CHECK(parsed->Layout == 7);
    CHECK(parsed->Button == "vm_row3");
}

TEST_CASE("A multi-byte handle is decoded as a varint, not a byte")
{
    // 0x96 0x01 is 150: the low seven bits of each byte, least significant first.
    std::string bytes;
    bytes += Key(1, 0);
    bytes += static_cast<char>(0x96);
    bytes += static_cast<char>(0x01);
    bytes += Key(2, 2);
    bytes += static_cast<char>(2);
    bytes += "ok";

    auto parsed = ParseClickPayload(bytes);
    REQUIRE(parsed.has_value());
    CHECK(parsed->Layout == 150);
    CHECK(parsed->Button == "ok");
}

TEST_CASE("The fields are read wherever they sit")
{
    std::string bytes;
    bytes += Key(2, 2);
    bytes += static_cast<char>(6);
    bytes += "accept";
    bytes += Key(1, 0);
    bytes += static_cast<char>(9);

    auto parsed = ParseClickPayload(bytes);
    REQUIRE(parsed.has_value());
    CHECK(parsed->Layout == 9);
    CHECK(parsed->Button == "accept");
}

TEST_CASE("An empty button id is a value, not a failure")
{
    auto parsed = ParseClickPayload(Press(1, ""));
    REQUIRE(parsed.has_value());
    CHECK(parsed->Button.empty());
}

TEST_CASE("A field CS2 adds later is skipped rather than refused")
{
    for (int wireType : {0, 1, 2, 5})
    {
        std::string bytes = Press(4, "decline");
        bytes += Key(9, wireType);
        switch (wireType)
        {
        case 0:
            bytes += static_cast<char>(42);
            break;
        case 1:
            bytes.append(8, 'x');
            break;
        case 2:
            bytes += static_cast<char>(3);
            bytes += "abc";
            break;
        default:
            bytes.append(4, 'x');
            break;
        }

        CAPTURE(wireType);
        auto parsed = ParseClickPayload(bytes);
        REQUIRE(parsed.has_value());
        CHECK(parsed->Layout == 4);
        CHECK(parsed->Button == "decline");
    }
}

TEST_CASE("A payload missing either field is refused")
{
    std::string layoutOnly;
    layoutOnly += Key(1, 0);
    layoutOnly += static_cast<char>(3);
    CHECK(ParseClickPayload(layoutOnly).error().Code == ErrorCode::Invalid);

    std::string buttonOnly;
    buttonOnly += Key(2, 2);
    buttonOnly += static_cast<char>(2);
    buttonOnly += "hi";
    CHECK(ParseClickPayload(buttonOnly).error().Code == ErrorCode::Invalid);

    CHECK(ParseClickPayload("").error().Code == ErrorCode::Invalid);
}

TEST_CASE("A length that runs past the end is refused rather than read")
{
    std::string bytes;
    bytes += Key(1, 0);
    bytes += static_cast<char>(1);
    bytes += Key(2, 2);
    bytes += static_cast<char>(64);  // claims 64 bytes and supplies two
    bytes += "hi";

    CHECK(ParseClickPayload(bytes).error().Code == ErrorCode::Invalid);
}

TEST_CASE("A truncated varint is refused rather than read past the end")
{
    std::string bytes;
    bytes += Key(1, 0);
    bytes += static_cast<char>(0x80);  // continuation bit set, nothing follows

    CHECK(ParseClickPayload(bytes).error().Code == ErrorCode::Invalid);
}

TEST_CASE("A wire type that carries no length is refused, since the rest cannot be found")
{
    std::string bytes = Press(1, "ok");
    bytes += Key(9, 3);  // a proto2 group, which proto3 does not emit

    CHECK(ParseClickPayload(bytes).error().Code == ErrorCode::Invalid);
}

TEST_CASE("A button id holding a NUL is returned intact, for the caller to reject")
{
    std::string button("a\0b", 3);
    auto parsed = ParseClickPayload(Press(2, button));
    REQUIRE(parsed.has_value());
    CHECK(parsed->Button.size() == 3);
}

TEST_CASE("A skipped field claiming a huge length is refused, not wrapped around")
{
    std::string bytes = Press(1, "ok");
    bytes += Key(9, 2);
    // A 10-byte varint of all-ones: 2^64-1, which would wrap the cursor if it were simply added.
    bytes.append(9, static_cast<char>(0xFF));
    bytes += static_cast<char>(0x01);

    CHECK(ParseClickPayload(bytes).error().Code == ErrorCode::Invalid);
}
