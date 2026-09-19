#include "Support/TempPath.hpp"
#include "Support/TestLanguages.hpp"

#include <VoltMod/Core/Text/Translations.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::Translations;
using VoltModTests::TempDir;
using VoltModTests::TestLanguages;

TEST_CASE("A key missing from the player's language falls back to the active one before English")
{
    TempDir dir("langs");
    // Only "en" carries every key; "de" is the server language and "ru" the player's.
    dir.Write("en.json", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de.json", R"({"greet": "hallo", "shared": "geteilt"})");
    dir.Write("ru.json", R"({"greet": "privet"})");

    TestLanguages languages;
    Translations texts{languages};
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
    TempDir dir("langs");
    dir.Write("en.json", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de.json", R"({"greet": "hallo"})");

    TestLanguages languages;
    Translations texts{languages};
    REQUIRE(texts.Load(dir.Path()));
    texts.SetLanguage("de");

    CHECK(texts.Get("greet", 0) == "hallo");
    CHECK(texts.Get("onlyEn", 0) == "english");
}

TEST_CASE("The player's language answers, and clearing it falls back to the active one")
{
    TempDir dir("langs");
    dir.Write("en.json", R"({"greet": "hello"})");
    dir.Write("ru.json", R"({"greet": "privet"})");

    TestLanguages languages;
    Translations texts{languages};
    REQUIRE(texts.Load(dir.Path()));

    // Another plugin's pick reaches this one through the shared table.
    languages.Languages[0] = "ru";
    CHECK(texts.Get("greet", 0) == "privet");
    CHECK(texts.PlayerLanguage(0) == "ru");

    texts.SetPlayerLanguage(0, "");
    CHECK(languages.Languages[0].empty());
    CHECK(texts.Get("greet", 0) == "hello");
}
