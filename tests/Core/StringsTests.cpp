#include <VoltMod/Core/Strings.hpp>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using VoltMod::Strings;

TEST_CASE("Strings::ToLower")
{
    CHECK_EQ(Strings::ToLower("HeLLo"), std::string("hello"));
    CHECK_EQ(Strings::ToLower("ABC123"), std::string("abc123"));
    CHECK_EQ(Strings::ToLower(""), std::string(""));
}

TEST_CASE("Strings::Trim")
{
    CHECK_EQ(Strings::Trim("  hi  "), std::string("hi"));
    CHECK_EQ(Strings::Trim("\t\n hi \r\n"), std::string("hi"));
    CHECK_EQ(Strings::Trim("nopad"), std::string("nopad"));
    CHECK_EQ(Strings::Trim("   "), std::string(""));
    CHECK_EQ(Strings::Trim(""), std::string(""));
}

TEST_CASE("Strings::Join")
{
    std::vector<std::string> parts = {"a", "b", "c"};
    CHECK_EQ(Strings::Join(parts, ","), std::string("a,b,c"));
    CHECK_EQ(Strings::Join(parts, " - "), std::string("a - b - c"));

    std::vector<std::string> one = {"solo"};
    CHECK_EQ(Strings::Join(one, ","), std::string("solo"));

    std::vector<std::string> none;
    CHECK_EQ(Strings::Join(none, ","), std::string(""));
}

TEST_CASE("Strings::JoinNonEmpty skips empty pieces")
{
    std::vector<std::string> gaps{"reason", "", "appeal"};
    CHECK_EQ(Strings::JoinNonEmpty(gaps, " | "), std::string("reason | appeal"));

    std::vector<std::string> leading{"", "only"};
    CHECK_EQ(Strings::JoinNonEmpty(leading, " | "), std::string("only"));

    std::vector<std::string> trailing{"only", ""};
    CHECK_EQ(Strings::JoinNonEmpty(trailing, " | "), std::string("only"));

    std::vector<std::string> allEmpty{"", "", ""};
    CHECK_EQ(Strings::JoinNonEmpty(allEmpty, " | "), std::string(""));

    CHECK_EQ(Strings::JoinNonEmpty({}, " | "), std::string(""));
}

TEST_CASE("Strings::JoinNonEmpty matches Join when nothing is empty")
{
    std::vector<std::string> parts{"a", "b", "c"};
    CHECK_EQ(Strings::JoinNonEmpty(parts, ","), Strings::Join(parts, ","));
}

TEST_CASE("Strings::IsNumeric")
{
    CHECK(Strings::IsNumeric("12345"));
    CHECK(Strings::IsNumeric("0"));
    CHECK(!Strings::IsNumeric(""));
    CHECK(!Strings::IsNumeric("12a"));
    CHECK(!Strings::IsNumeric("-5"));
    CHECK(!Strings::IsNumeric("1.5"));
    CHECK(!Strings::IsNumeric(" 5"));
}

TEST_CASE("Strings::StartsWith")
{
    CHECK(Strings::StartsWith("hello world", "hello"));
    CHECK(Strings::StartsWith("abc", "abc"));
    CHECK(Strings::StartsWith("abc", ""));
    CHECK(!Strings::StartsWith("abc", "abcd"));
    CHECK(!Strings::StartsWith("abc", "xyz"));
}

TEST_CASE("Strings::DisplayNameOr")
{
    CHECK_EQ(Strings::DisplayNameOr(76561197960287930, ""), std::string("76561197960287930"));
    CHECK_EQ(Strings::DisplayNameOr(1, "Bob"), std::string("Bob"));
    CHECK_EQ(Strings::DisplayNameOr(1, "abcdef", 4), std::string("abcd..."));
}

TEST_CASE("Strings::EscapeHtml")
{
    CHECK_EQ(Strings::EscapeHtml("a & b < c > d \" e ' f"), std::string("a &amp; b &lt; c &gt; d &quot; e &#39; f"));
    CHECK_EQ(Strings::EscapeHtml("plain text"), std::string("plain text"));
    CHECK_EQ(Strings::EscapeHtml(""), std::string(""));
    CHECK_EQ(Strings::EscapeHtml("<script>"), std::string("&lt;script&gt;"));
}

TEST_CASE("Strings::TruncateUtf8")
{
    CHECK_EQ(Strings::TruncateUtf8("short", 40), std::string("short"));
    CHECK_EQ(Strings::TruncateUtf8("abcdef", 4), std::string("abcd..."));
    CHECK_EQ(Strings::TruncateUtf8("abcdef", 6), std::string("abcdef"));  // exact fit, no ellipsis

    // Cyrillic is 2 bytes per character; a cut at byte 5 would split the third character.
    std::string cyrillic = "\xD0\xB0\xD0\xB1\xD0\xB2";  // "абв"
    CHECK_EQ(Strings::TruncateUtf8(cyrillic, 5), std::string("\xD0\xB0\xD0\xB1..."));

    CHECK_EQ(Strings::TruncateUtf8("abcdef", 4, ""), std::string("abcd"));  // custom ellipsis
}
