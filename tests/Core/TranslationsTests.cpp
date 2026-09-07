#include "Support/TempPath.hpp"

#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <doctest/doctest.h>
#include <string>

using VoltMod::SlotEvents;
using VoltMod::Translations;
using VoltModTests::TempDir;

TEST_CASE("A key missing from the player's language falls back to the active one before English")
{
    TempDir dir("langs");
    // Only "en" carries every key; "de" is the server language and "ru" the player's.
    dir.Write("en.json", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de.json", R"({"greet": "hallo", "shared": "geteilt"})");
    dir.Write("ru.json", R"({"greet": "privet"})");

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
    TempDir dir("langs");
    dir.Write("en.json", R"({"greet": "hello", "onlyEn": "english"})");
    dir.Write("de.json", R"({"greet": "hallo"})");

    SlotEvents slots;
    Translations texts{slots};
    REQUIRE(texts.Load(dir.Path()));
    texts.SetLanguage("de");

    CHECK(texts.Get("greet", 0) == "hallo");
    CHECK(texts.Get("onlyEn", 0) == "english");
}
