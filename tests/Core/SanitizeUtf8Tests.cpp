#include <VoltMod/Core/Strings.hpp>
#include <doctest/doctest.h>
#include <string>

// Player names arrive from the client and need not be valid UTF-8. The JSON writer passes bytes
// through, so anything bound for a webhook or a panel goes through here first.

using VoltMod::Strings;

static const std::string Replacement = "\xEF\xBF\xBD";  // U+FFFD

TEST_CASE("SanitizeUtf8 leaves well-formed text untouched")
{
    CHECK(Strings::SanitizeUtf8("plain ascii") == "plain ascii");
    CHECK(Strings::SanitizeUtf8("\xD0\x9F\xD1\x80\xD0\xB8") == "\xD0\x9F\xD1\x80\xD0\xB8");  // Cyrillic
    CHECK(Strings::SanitizeUtf8("\xE2\x9C\x93") == "\xE2\x9C\x93");                          // U+2713
    CHECK(Strings::SanitizeUtf8("\xF0\x9F\x98\x80") == "\xF0\x9F\x98\x80");                  // U+1F600
    CHECK(Strings::SanitizeUtf8("").empty());
}

TEST_CASE("SanitizeUtf8 replaces a lone continuation byte")
{
    CHECK(Strings::SanitizeUtf8("a\x80z") == "a" + Replacement + "z");
}

TEST_CASE("SanitizeUtf8 replaces a truncated sequence")
{
    // A two-byte lead with nothing after it, and a three-byte lead one byte short.
    CHECK(Strings::SanitizeUtf8("a\xD0") == "a" + Replacement);
    CHECK(Strings::SanitizeUtf8("a\xE2\x9C") == "a" + Replacement + Replacement);
}

TEST_CASE("SanitizeUtf8 replaces an overlong encoding")
{
    // A two-byte encoding of NUL would otherwise slip past a check that reads the decoded string.
    CHECK(Strings::SanitizeUtf8("\xC0\x80") == Replacement + Replacement);
    CHECK(Strings::SanitizeUtf8("\xE0\x80\xAF") == Replacement + Replacement + Replacement);
}

TEST_CASE("SanitizeUtf8 replaces a surrogate and anything past U+10FFFF")
{
    CHECK(Strings::SanitizeUtf8("\xED\xA0\x80") != "\xED\xA0\x80");  // U+D800
    CHECK(Strings::SanitizeUtf8("\xF5\x80\x80\x80") != "\xF5\x80\x80\x80");
}

TEST_CASE("SanitizeUtf8 replaces the 5-byte and 6-byte forms UTF-8 never had")
{
    CHECK(Strings::SanitizeUtf8("\xF8\x88\x80\x80\x80") != "\xF8\x88\x80\x80\x80");
    CHECK(Strings::SanitizeUtf8("\xFE") == Replacement);
}

TEST_CASE("TruncateUtf8 does not hand back bytes that were already malformed")
{
    // Short enough to skip the cut entirely; it must still be sanitized.
    CHECK(Strings::TruncateUtf8("a\xFF", 64) == "a" + Replacement);
}

TEST_CASE("TruncateUtf8 still cuts on a sequence boundary")
{
    // Six bytes of Cyrillic cut at four keeps two whole characters plus the ellipsis.
    const std::string text = "\xD0\x9F\xD1\x80\xD0\xB8";
    const std::string cut = Strings::TruncateUtf8(text, 4, "...");
    CHECK(cut == "\xD0\x9F\xD1\x80...");
}
