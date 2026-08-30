#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <cstdio>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// These are at file scope because Glaze reflects their member names.

struct Nested
{
    int port = 5432;
    std::string sslMode = "prefer";
};

struct Sample
{
    std::string host = "localhost";
    Nested database;
    std::vector<std::string> tags = {"a", "b"};
};

/** Writes @p text to a temporary file and removes it again. */
class TempFile
{
public:
    explicit TempFile(std::string_view text)
        : _path(std::filesystem::temp_directory_path() /
                std::format("voltmod-json-{}.jsonc", reinterpret_cast<uintptr_t>(this)))
    {
        std::ofstream out(_path, std::ios::binary);
        out << text;
    }

    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(_path, ignored);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;

    std::string Path() const { return _path.string(); }

private:
    std::filesystem::path _path;
};

using VoltMod::ErrorCode;
using VoltMod::Json;

TEST_CASE("Json reads a well-formed document into its struct")
{
    auto parsed = Json::Read<Sample>(R"({"host":"db.example","database":{"port":6000,"sslMode":"require"}})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->host == "db.example");
    CHECK(parsed->database.port == 6000);
    CHECK(parsed->database.sslMode == "require");
}

TEST_CASE("Json keeps a member initializer for every key the document omits")
{
    auto parsed = Json::Read<Sample>(R"({"host":"only-host"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->host == "only-host");
    // This is the WITH_DEFAULT contract the removed macros used to provide.
    CHECK(parsed->database.port == 5432);
    CHECK(parsed->database.sslMode == "prefer");
    CHECK(parsed->tags == std::vector<std::string>{"a", "b"});
}

TEST_CASE("Json tolerates comments, including one before the opening brace")
{
    // detections.jsonc opens with a comment block, so this shape has to keep working.
    auto parsed = Json::Read<Sample>("// what this file is\n/* and why */\n{\n  \"host\": \"x\" // trailing\n}");
    REQUIRE(parsed.has_value());
    CHECK(parsed->host == "x");
}

TEST_CASE("Json rejects a wrong-typed value and says which key")
{
    auto parsed = Json::Read<Sample>(R"({"database":{"port":"not-a-number"}})");
    REQUIRE_FALSE(parsed.has_value());
    CHECK(parsed.error().Code == ErrorCode::Invalid);
    CHECK_FALSE(parsed.error().Detail.empty());
}

TEST_CASE("Json ignores unknown keys for settings compatibility")
{
    auto parsed = Json::Read<Sample>(R"({"hsot":"typo"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->host == "localhost");
}

TEST_CASE("Json rejects invalid UTF-8 in a document it owns")
{
    auto parsed = Json::Read<Sample>("{\"host\":\"a\xFF\"}");
    CHECK_FALSE(parsed.has_value());
}

TEST_CASE("Json distinguishes a missing file from a malformed one")
{
    auto missing = Json::ReadFile<Sample>("definitely/not/here.jsonc");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().Code == ErrorCode::NotFound);

    const TempFile bad(R"({ not json)");
    auto malformed = Json::ReadFile<Sample>(bad.Path());
    REQUIRE_FALSE(malformed.has_value());
    CHECK(malformed.error().Code == ErrorCode::Invalid);
}

TEST_CASE("Json reads a file through the resolved path")
{
    const TempFile file(R"({"host":"from-file"})");
    auto parsed = Json::ReadFile<Sample>(file.Path());
    REQUIRE(parsed.has_value());
    CHECK(parsed->host == "from-file");
}

TEST_CASE("SubstituteTokens rewrites strings anywhere in the document")
{
    auto document = Json::ParseDocument(R"({"a":"{name}","b":{"c":"{id}"},"d":["{name}",1]})");
    REQUIRE(document.has_value());

    Json::SubstituteTokens(*document, {{"name", "Ada"}, {"id", "42"}});
    const auto written = Json::Write(*document);
    CHECK(written.find("Ada") != std::string::npos);
    CHECK(written.find("42") != std::string::npos);
    CHECK(written.find("{name}") == std::string::npos);
    CHECK(written.find("{id}") == std::string::npos);
}

TEST_CASE("SubstituteTokens escapes a token value that would otherwise break the document")
{
    // A player name is client-controlled; substituting into the parsed document rather than its
    // text is what keeps a quote or a backslash from producing JSON nobody can parse.
    auto document = Json::ParseDocument(R"({"player":"{name}"})");
    REQUIRE(document.has_value());

    Json::SubstituteTokens(*document, {{"name", R"(a" \ b)"}});
    const auto written = Json::Write(*document);

    auto reparsed = Json::ParseDocument(written);
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->at("player").get_string() == R"(a" \ b)");
}

TEST_CASE("GetStringByPath descends a dot path and renders primitives without quotes")
{
    const std::string body = R"({"data":{"room":{"code":42,"name":"lobby","open":true}}})";
    CHECK(Json::GetStringByPath(body, "data.room.name") == "lobby");
    CHECK(Json::GetStringByPath(body, "data.room.code") == "42");
    CHECK(Json::GetStringByPath(body, "data.room.open") == "true");
}

TEST_CASE("GetStringByPath returns nothing rather than failing on bad input")
{
    const std::string body = R"({"data":{"room":{"code":42}}})";
    CHECK(Json::GetStringByPath(body, "data.room.missing").empty());
    CHECK(Json::GetStringByPath(body, "nope.at.all").empty());
    CHECK(Json::GetStringByPath(body, "data.room").empty());  // an object is not a leaf
    CHECK(Json::GetStringByPath("{ not json", "a.b").empty());
    CHECK(Json::GetStringByPath("", "a").empty());
}

TEST_CASE("Write round-trips a reflected aggregate")
{
    const Sample sample{.host = "h", .database = {.port = 1, .sslMode = "disable"}, .tags = {"z"}};
    auto reparsed = Json::Read<Sample>(Json::Write(sample));
    REQUIRE(reparsed.has_value());
    CHECK(reparsed->host == "h");
    CHECK(reparsed->database.port == 1);
    CHECK(reparsed->tags == std::vector<std::string>{"z"});
}
