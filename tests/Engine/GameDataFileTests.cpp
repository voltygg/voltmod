#include "Engine/GameDataFile.hpp"

#include <doctest/doctest.h>
#include <string>
#include <string_view>

using VoltMod::ErrorCode;
using VoltMod::GameDataFile;
using VoltMod::GamePlatform;
using VoltMod::IsValidBytePattern;

/** A minimal but complete v2 document, so each test can vary exactly one thing. */
static std::string Document(std::string_view sections)
{
    return std::string(R"({
  "version": 2,
  "build": { "game": "cs2", "verified": "2026-08-26", "note": "n" },
)") + std::string(sections) +
           "\n}";
}

static std::string Detail(const VoltMod::Error& error)
{
    return error.Detail;
}

TEST_CASE("GameDataFile parses every section and keeps the requested platform")
{
    const auto text = Document(R"(  "signatures": {
    "Sig": { "library": "engine2", "windows": { "pattern": "48 8B ? ??" }, "linux": { "pattern": "55 48" } }
  },
  "addresses": {
    "Addr": { "signature": "Sig", "rel32At": { "windows": 3, "linux": 7 } }
  },
  "vtables": {
    "Slot": { "class": "CFoo", "windows": 25, "linux": 26 }
  },
  "offsets": {
    "Field": { "windows": 16, "linux": 24, "max": 512, "align": 8 }
  })");

    auto windows = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE(windows.has_value());
    CHECK(windows->Version == 2);
    CHECK(windows->Build.Game == "cs2");
    CHECK(windows->Build.Verified == "2026-08-26");
    CHECK(windows->EntryCount() == 4);
    CHECK(windows->Signatures.at("Sig").Library == "engine2");
    CHECK(windows->Signatures.at("Sig").Pattern == "48 8B ? ??");
    CHECK(windows->Addresses.at("Addr").Signature == "Sig");
    CHECK(windows->Addresses.at("Addr").Rel32At == 3);
    CHECK(windows->VTables.at("Slot").Class == "CFoo");
    CHECK(windows->VTables.at("Slot").Library == "server");  // defaulted
    CHECK(windows->VTables.at("Slot").Index == 25);
    CHECK(windows->Offsets.at("Field").Value == 16);
    CHECK(windows->Offsets.at("Field").Max == 512);
    CHECK(windows->Offsets.at("Field").Align == 8);

    // Not named `linux`: GCC predefines that as a macro on the platform this column describes.
    auto elf = GameDataFile::Parse(text, GamePlatform::Linux);
    REQUIRE(elf.has_value());
    CHECK(elf->Signatures.at("Sig").Pattern == "55 48");
    CHECK(elf->Addresses.at("Addr").Rel32At == 7);
    CHECK(elf->VTables.at("Slot").Index == 26);
    CHECK(elf->Offsets.at("Field").Value == 24);
}

TEST_CASE("GameDataFile rejects a document with no version")
{
    auto parsed = GameDataFile::Parse(R"({ "build": { "game": "cs2" } })", GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().Code == ErrorCode::Invalid);
    CHECK(Detail(parsed.error()).find("version") != std::string::npos);
}

TEST_CASE("GameDataFile rejects an unsupported version")
{
    auto parsed = GameDataFile::Parse(R"({ "version": 1 })", GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("unsupported version 1") != std::string::npos);
}

TEST_CASE("GameDataFile rejects a key declared in two sections")
{
    const auto text =
        Document(R"(  "signatures": { "Both": { "windows": { "pattern": "48" }, "linux": { "pattern": "48" } } },
  "offsets": { "Both": { "windows": 8, "linux": 8 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("'Both' is declared in both 'signatures' and 'offsets'") != std::string::npos);
}

// gamedata.schema.json requires one platform column, not both: something located on Windows only
// is a capability that is off on Linux, not a file that fails to parse there.
TEST_CASE("GameDataFile keeps a single-platform entry out of the other platform's maps")
{
    const auto text = Document(R"(  "offsets": { "WindowsOnly": { "windows": 8 } })");

    auto windows = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE(windows.has_value());
    CHECK(windows->Offsets.contains("WindowsOnly"));
    CHECK(windows->OtherPlatformOnly.empty());

    auto linux = GameDataFile::Parse(text, GamePlatform::Linux);
    REQUIRE(linux.has_value());
    CHECK_FALSE(linux->Offsets.contains("WindowsOnly"));
    // Named, so an unavailable feature can say more than "not in gamedata".
    REQUIRE(linux->OtherPlatformOnly.size() == 1);
    CHECK(linux->OtherPlatformOnly.front() == "WindowsOnly");
}

TEST_CASE("GameDataFile rejects an entry with no column for either platform")
{
    const auto text = Document(R"(  "offsets": { "Nowhere": { "max": 64 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("has no 'windows' entry") != std::string::npos);
}

// An address is resolved from its signature's match, so it has to go wherever that signature goes
// rather than read as a reference to a signature nobody wrote.
TEST_CASE("GameDataFile drops an address whose signature is for the other platform")
{
    const auto text = Document(
        R"(  "signatures": { "Sig": { "windows": { "pattern": "48 8B" } } },)"
        "\n"
        R"(  "addresses": { "Addr": { "signature": "Sig", "rel32At": { "windows": 3, "linux": 3 } } })");

    auto linux = GameDataFile::Parse(text, GamePlatform::Linux);
    REQUIRE(linux.has_value());
    CHECK(linux->Addresses.empty());
    CHECK(linux->Signatures.empty());
}

TEST_CASE("GameDataFile rejects a malformed byte pattern")
{
    const auto text =
        Document(R"(  "signatures": { "Sig": { "windows": { "pattern": "48 ZZ" }, "linux": { "pattern": "48" } } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("is not a byte pattern") != std::string::npos);
}

TEST_CASE("GameDataFile rejects an address deriving from an unknown signature")
{
    const auto text =
        Document(R"(  "addresses": { "Addr": { "signature": "Nope", "rel32At": { "windows": 3, "linux": 3 } } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("unknown signature 'Nope'") != std::string::npos);
}

TEST_CASE("GameDataFile rejects a negative rel32At")
{
    const auto text =
        Document(R"(  "signatures": { "Sig": { "windows": { "pattern": "48" }, "linux": { "pattern": "48" } } },
  "addresses": { "Addr": { "signature": "Sig", "rel32At": { "windows": -4, "linux": 3 } } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("rel32At is negative") != std::string::npos);
}

TEST_CASE("GameDataFile rejects a vtable index outside the accepted range")
{
    const auto text = Document(R"(  "vtables": { "Slot": { "class": "CFoo", "windows": 500, "linux": 26 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("outside [0, 500)") != std::string::npos);

    const auto negative = Document(R"(  "vtables": { "Slot": { "class": "CFoo", "windows": -1, "linux": 26 } })");
    CHECK_FALSE(GameDataFile::Parse(negative, GamePlatform::Windows).has_value());
}

TEST_CASE("GameDataFile rejects a vtable entry with no class")
{
    const auto text = Document(R"(  "vtables": { "Slot": { "windows": 25, "linux": 26 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("has no 'class'") != std::string::npos);
}

TEST_CASE("GameDataFile rejects an offset above its own max")
{
    const auto text = Document(R"(  "offsets": { "Field": { "windows": 600, "linux": 8, "max": 512 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("outside [0, 512]") != std::string::npos);
}

TEST_CASE("GameDataFile rejects a misaligned offset")
{
    const auto text = Document(R"(  "offsets": { "Field": { "windows": 12, "linux": 8, "align": 8 } })");

    auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("not aligned to 8") != std::string::npos);
}

TEST_CASE("GameDataFile reports malformed JSON rather than throwing")
{
    auto parsed = GameDataFile::Parse("{ not json", GamePlatform::Windows);
    REQUIRE_FALSE(parsed.has_value());
    CHECK(Detail(parsed.error()).find("not valid JSON") != std::string::npos);
}

TEST_CASE("IsValidBytePattern accepts hex bytes and wildcards and nothing else")
{
    CHECK(IsValidBytePattern("48"));
    CHECK(IsValidBytePattern("48 8B 0D"));
    CHECK(IsValidBytePattern("48 ? 8B ?? 0D"));
    CHECK(IsValidBytePattern("ff aB 00"));

    CHECK_FALSE(IsValidBytePattern(""));
    CHECK_FALSE(IsValidBytePattern("4"));
    CHECK_FALSE(IsValidBytePattern("488B"));
    CHECK_FALSE(IsValidBytePattern("48 ZZ"));
    CHECK_FALSE(IsValidBytePattern(" 48"));
    CHECK_FALSE(IsValidBytePattern("48 "));
    CHECK_FALSE(IsValidBytePattern("48  8B"));
    CHECK_FALSE(IsValidBytePattern("48 ???"));
}

// Parse is called from Runtime::Start. Before these, only a JSON *syntax* error was caught, so
// structurally-wrong-but-syntactically-valid input threw nlohmann's type_error out of the load.

TEST_CASE("A section that is not an object is rejected rather than thrown out of")
{
    for (std::string_view section : {"signatures", "addresses", "vtables", "offsets"})
    {
        const auto text = Document(std::string("  \"") + std::string(section) + "\": [1, 2]");
        const auto parsed = GameDataFile::Parse(text, GamePlatform::Windows);

        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().Code == ErrorCode::Invalid);
        CHECK(Detail(parsed.error()).find(section) != std::string::npos);
    }
}

TEST_CASE("A scalar where an object belongs is rejected rather than thrown out of")
{
    for (std::string_view body : {
             R"(  "build": [1, 2])",
             R"(  "signatures": { "Sig": 7 })",
             R"(  "signatures": { "Sig": { "windows": "48 8B", "linux": "55" } })",
             R"(  "vtables": { "V": 3 })",
             R"(  "offsets": { "O": "12" })",
         })
    {
        const auto parsed = GameDataFile::Parse(Document(body), GamePlatform::Windows);

        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().Code == ErrorCode::Invalid);
    }
}

TEST_CASE("A wrong numeric type is rejected rather than thrown out of")
{
    const std::string sig = R"(  "signatures": {
    "Sig": { "windows": { "pattern": "48 8B" }, "linux": { "pattern": "55" } }
  },
)";

    for (std::string_view body : {
             R"(  "addresses": { "A": { "signature": "Sig", "rel32At": 3 } })",
             R"(  "addresses": { "A": { "signature": "Sig", "rel32At": { "windows": "3", "linux": 7 } } })",
             R"(  "vtables": { "V": { "class": "C", "windows": "3", "linux": 4 } })",
             R"(  "offsets": { "O": { "windows": 1.5, "linux": 8 } })",
             R"(  "offsets": { "O": { "max": "big", "windows": 8, "linux": 8 } })",
         })
    {
        const auto parsed = GameDataFile::Parse(Document(sig + std::string(body)), GamePlatform::Windows);

        REQUIRE_FALSE(parsed.has_value());
        CHECK(parsed.error().Code == ErrorCode::Invalid);
    }
}
