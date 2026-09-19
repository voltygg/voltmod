#include "Support/TempPath.hpp"

#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Text/PlayerLanguages.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <array>
#include <doctest/doctest.h>
#include <string>
#include <string_view>

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

/** Stands in for the host's table every plugin shares. */
class SharedTable final : public VoltMod::PlayerLanguages
{
public:
    std::string_view Language(int slot) const override { return Langs[slot]; }
    void SetLanguage(int slot, std::string_view lang) override { Langs[slot] = lang; }

    std::array<std::string, VoltMod::MaxPlayers> Langs;
};

TEST_CASE("With a shared table, a language any plugin sets answers in every plugin")
{
    TempDir dir("langs");
    dir.Write("en.json", R"({"greet": "hello"})");
    dir.Write("ru.json", R"({"greet": "privet"})");
    dir.Write("de.json", R"({"greet": "hallo"})");

    SlotEvents slots;
    SharedTable shared;
    Translations texts{slots};
    REQUIRE(texts.Load(dir.Path()));
    texts.UseSharedLanguages(shared);

    // Another plugin's pick reaches this one through the shared table.
    shared.Langs[0] = "ru";
    CHECK(texts.Get("greet", 0) == "privet");
    CHECK(texts.PlayerLanguage(0) == "ru");

    texts.SetPlayerLanguage(0, "de");
    CHECK(shared.Langs[0] == "de");
    CHECK(texts.Get("greet", 0) == "hallo");

    texts.SetPlayerLanguage(0, "");
    CHECK(texts.Get("greet", 0) == "hello");
}

TEST_CASE("Without a shared table a player's language stays in this plugin")
{
    TempDir dir("langs");
    dir.Write("en.json", R"({"greet": "hello"})");
    dir.Write("ru.json", R"({"greet": "privet"})");

    SlotEvents slots;
    Translations texts{slots};
    REQUIRE(texts.Load(dir.Path()));

    texts.SetPlayerLanguage(0, "ru");
    CHECK(texts.Get("greet", 0) == "privet");
}
