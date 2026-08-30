#include "Engine/GameDataFile.hpp"

#include <VoltMod/Core/Result.hpp>
#include <doctest/doctest.h>
#include <fstream>
#include <iterator>
#include <string>

// The reader rejects unknown keys, so a key the shipped file carries but the model does not know
// would now fail at server load. That must fail here instead.

#ifdef VOLTMOD_GAMEDATA_JSONC

using VoltMod::GamePlatform;

static std::string ShippedGameData()
{
    std::ifstream file(VOLTMOD_GAMEDATA_JSONC, std::ios::binary);
    REQUIRE_MESSAGE(file.is_open(), "cannot open " VOLTMOD_GAMEDATA_JSONC);
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

TEST_CASE("The shipped gamedata.jsonc parses for both platforms with nothing rejected")
{
    const std::string text = ShippedGameData();

    for (const GamePlatform platform : {GamePlatform::Windows, GamePlatform::Linux})
    {
        auto parsed = VoltMod::GameDataFile::Parse(text, platform);
        const std::string why = parsed.has_value() ? std::string{} : parsed.error().Detail;
        REQUIRE_MESSAGE(parsed.has_value(), why);
        CHECK(parsed->Version == VoltMod::GameDataFormatVersion);
        // The file names a build it was verified against; an empty one means the model dropped it.
        CHECK_FALSE(parsed->Build.Verified.empty());
        CHECK(parsed->EntryCount() > 0);
    }
}

TEST_CASE("The shipped gamedata.jsonc keeps its schema key")
{
    // `$schema` is what gives config authors editor completion; the reader has to accept it even
    // though unknown keys are otherwise an error.
    CHECK(ShippedGameData().find("$schema") != std::string::npos);
}

#endif  // VOLTMOD_GAMEDATA_JSONC
