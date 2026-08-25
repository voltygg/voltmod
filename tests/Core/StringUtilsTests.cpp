#include <VoltMod/Core/StringUtils.hpp>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::Core::StringUtils;

TEST_CASE("StringUtils::ToLower")
{
    CHECK_EQ(StringUtils::ToLower("HeLLo"), std::string("hello"));
    CHECK_EQ(StringUtils::ToLower("ABC123"), std::string("abc123"));
    CHECK_EQ(StringUtils::ToLower(""), std::string(""));
}

TEST_CASE("StringUtils::Trim")
{
    CHECK_EQ(StringUtils::Trim("  hi  "), std::string("hi"));
    CHECK_EQ(StringUtils::Trim("\t\n hi \r\n"), std::string("hi"));
    CHECK_EQ(StringUtils::Trim("nopad"), std::string("nopad"));
    CHECK_EQ(StringUtils::Trim("   "), std::string(""));
    CHECK_EQ(StringUtils::Trim(""), std::string(""));
    CHECK_EQ(StringUtils::TrimLeft("  x  "), std::string("x  "));
    CHECK_EQ(StringUtils::TrimRight("  x  "), std::string("  x"));
}

TEST_CASE("StringUtils::Split")
{
    auto parts = StringUtils::Split("a,b,c", ',');
    CHECK_EQ(parts.size(), static_cast<size_t>(3));
    CHECK_EQ(parts[0], std::string("a"));
    CHECK_EQ(parts[1], std::string("b"));
    CHECK_EQ(parts[2], std::string("c"));

    auto single = StringUtils::Split("noseparator", ',');
    CHECK_EQ(single.size(), static_cast<size_t>(1));
    CHECK_EQ(single[0], std::string("noseparator"));

    auto empty = StringUtils::Split("", ',');
    CHECK_EQ(empty.size(), static_cast<size_t>(0));
}

TEST_CASE("StringUtils::Join")
{
    std::vector<std::string> parts = {"a", "b", "c"};
    CHECK_EQ(StringUtils::Join(parts, ","), std::string("a,b,c"));
    CHECK_EQ(StringUtils::Join(parts, " - "), std::string("a - b - c"));

    std::vector<std::string> one = {"solo"};
    CHECK_EQ(StringUtils::Join(one, ","), std::string("solo"));

    std::vector<std::string> none;
    CHECK_EQ(StringUtils::Join(none, ","), std::string(""));
}

TEST_CASE("StringUtils::Split and Join round-trip")
{
    auto parts = StringUtils::Split("x|y|z", '|');
    CHECK_EQ(StringUtils::Join(parts, "|"), std::string("x|y|z"));
}

TEST_CASE("StringUtils::IsNumeric")
{
    CHECK(StringUtils::IsNumeric("12345"));
    CHECK(StringUtils::IsNumeric("0"));
    CHECK(!StringUtils::IsNumeric(""));
    CHECK(!StringUtils::IsNumeric("12a"));
    CHECK(!StringUtils::IsNumeric("-5"));
    CHECK(!StringUtils::IsNumeric("1.5"));
    CHECK(!StringUtils::IsNumeric(" 5"));
}

TEST_CASE("StringUtils::StartsWith")
{
    CHECK(StringUtils::StartsWith("hello world", "hello"));
    CHECK(StringUtils::StartsWith("abc", "abc"));
    CHECK(StringUtils::StartsWith("abc", ""));
    CHECK(!StringUtils::StartsWith("abc", "abcd"));
    CHECK(!StringUtils::StartsWith("abc", "xyz"));
}

TEST_CASE("StringUtils::EndsWith")
{
    CHECK(StringUtils::EndsWith("hello world", "world"));
    CHECK(StringUtils::EndsWith("abc", "abc"));
    CHECK(StringUtils::EndsWith("abc", ""));
    CHECK(!StringUtils::EndsWith("abc", "dabc"));
    CHECK(!StringUtils::EndsWith("abc", "xyz"));
}

TEST_CASE("StringUtils::DisplayNameOr")
{
    CHECK_EQ(StringUtils::DisplayNameOr(76561197960287930, ""), std::string("76561197960287930"));
    CHECK_EQ(StringUtils::DisplayNameOr(1, "Bob"), std::string("Bob"));
    CHECK_EQ(StringUtils::DisplayNameOr(1, "abcdef", 4), std::string("abcd..."));
}

TEST_CASE("StringUtils::EscapeHtml")
{
    CHECK_EQ(StringUtils::EscapeHtml("a & b < c > d \" e ' f"),
             std::string("a &amp; b &lt; c &gt; d &quot; e &#39; f"));
    CHECK_EQ(StringUtils::EscapeHtml("plain text"), std::string("plain text"));
    CHECK_EQ(StringUtils::EscapeHtml(""), std::string(""));
    CHECK_EQ(StringUtils::EscapeHtml("<script>"), std::string("&lt;script&gt;"));
}

TEST_CASE("StringUtils::TruncateUtf8")
{
    CHECK_EQ(StringUtils::TruncateUtf8("short", 40), std::string("short"));
    CHECK_EQ(StringUtils::TruncateUtf8("abcdef", 4), std::string("abcd..."));
    CHECK_EQ(StringUtils::TruncateUtf8("abcdef", 6), std::string("abcdef"));  // exact fit, no ellipsis

    // Cyrillic is 2 bytes per character; a cut at byte 5 would split the third character.
    std::string cyrillic = "\xD0\xB0\xD0\xB1\xD0\xB2";  // "абв"
    CHECK_EQ(StringUtils::TruncateUtf8(cyrillic, 5), std::string("\xD0\xB0\xD0\xB1..."));

    CHECK_EQ(StringUtils::TruncateUtf8("abcdef", 4, ""), std::string("abcd"));  // custom ellipsis
}
