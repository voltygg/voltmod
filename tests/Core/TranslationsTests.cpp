#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <doctest/doctest.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>

using VoltMod::SlotEvents;
using VoltMod::Translations;

/** A directory of `<lang>.json` files, removed again when the test ends. */
class TempLangDir
{
public:
    TempLangDir()
        : _path(std::filesystem::temp_directory_path() /
                std::format("voltmod-langs-{}", reinterpret_cast<uintptr_t>(this)))
    {
        std::filesystem::create_directories(_path);
    }

    ~TempLangDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(_path, ignored);
    }

    TempLangDir(const TempLangDir&) = delete;
    TempLangDir& operator=(const TempLangDir&) = delete;

    void Write(std::string_view lang, std::string_view json) const
    {
        std::ofstream out(_path / std::format("{}.json", lang), std::ios::binary);
        out << json;
    }

    std::string Path() const { return _path.string(); }

private:
    std::filesystem::path _path;
};

TEST_CASE("A key missing from the player's language falls back to the active one before English")
{
    TempLangDir dir;
    // Only "en" carries every key; "de" is the server language and "ru" the player's.
    dir.Write("en", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de", R"({"greet": "hallo", "shared": "geteilt"})");
    dir.Write("ru", R"({"greet": "privet"})");

    SlotEvents slots;
    Translations texts{slots};
    REQUIRE(texts.Load(dir.Path()));
    texts.SetLanguage("de");
    texts.SetPlayerLanguage(0, "ru");

    // Present in the player's own language: that wins outright.
    CHECK(texts.Get("greet", 0) == "privet");

    // Missing from "ru" but present in the active language - this is the step that used to be
    // skipped, sending a Russian-speaking player on a German server straight to English.
    CHECK(texts.Get("shared", 0) == "geteilt");

    // Missing from both: English is still the floor.
    CHECK(texts.Get("onlyEn", 0) == "english");

    // Missing everywhere: the key itself, so nothing renders blank.
    CHECK(texts.Get("absent", 0) == "absent");
}

TEST_CASE("With no player language the active language answers, then English")
{
    TempLangDir dir;
    dir.Write("en", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de", R"({"greet": "hallo"})");

    SlotEvents slots;
    Translations texts{slots};
    REQUIRE(texts.Load(dir.Path()));
    texts.SetLanguage("de");

    CHECK(texts.Get("greet", 0) == "hallo");
    CHECK(texts.Get("onlyEn", 0) == "english");
}
