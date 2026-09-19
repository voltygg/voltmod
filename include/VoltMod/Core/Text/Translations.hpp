#pragma once

#include <VoltMod/Core/Text/PlayerLanguages.hpp>
#include <VoltMod/Core/Text/StringMap.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Localization system. Loads one JSON file per language; nested objects flatten into
 * dotted keys (`category.punish`). Use @ref Get(key, slot) for per-player text.
 *
 * Lookup order: the slot's language (@ref PlayerLanguage), the active language, English, the
 * framework's English defaults for its own keys (`cmd.*`, `target.*`), then the key verbatim.
 */
class Translations
{
public:
    /** @p languages holds each player's pick and must outlive this. */
    explicit Translations(PlayerLanguages& languages);

    bool Load(std::string_view dirPath);
    void SetLanguage(std::string_view lang);
    const std::string& GetLanguage() const;

    /** Look up a key in the active (server) language, falling back to English. For broadcasts;
     *  prefer @ref Get(key, slot) for any message addressed to a specific player. */
    std::string Get(std::string_view key) const;

    /** Look up a key in @p slot's registered language, falling back to the active language then English. */
    std::string Get(std::string_view key, int slot) const;

    /** @ref Get(key, slot), but @p fallback instead of the key itself when nothing carries it.
     *  For text the framework or a plugin can render without the consumer shipping the key. */
    std::string GetOr(std::string_view key, int slot, std::string_view fallback) const;

    /** @ref Get(key, slot), then replace each `{token}` occurrence from @p tokens. */
    std::string Get(std::string_view key, int slot, const Tokens& tokens) const;

    /** Active-language variant of the token-substituting @ref Get. */
    std::string Get(std::string_view key, const Tokens& tokens) const;

    /** Language codes that were successfully loaded (one per JSON file). */
    std::vector<std::string> GetAvailableLanguages() const;

    /** Set @p slot's language for every plugin. Empty means the active language. */
    void SetPlayerLanguage(int slot, std::string_view lang);

    /** Empty means the active language. Valid until the next change. */
    [[nodiscard]] std::string_view PlayerLanguage(int slot) const;

private:
    // Engaged (possibly with an empty view) when lang/key is present, nullopt when it is absent.
    // A view, not a string, so a lookup on the per-frame menu path copies nothing; the tables and
    // the built-in defaults both outlive the call.
    std::optional<std::string_view> LookupIn(std::string_view lang, std::string_view key) const;

    // The whole lookup chain for @p slot short of the final fallback: nullopt means nothing
    // carries the key, which is what Get and GetOr answer differently.
    std::optional<std::string_view> Resolve(std::string_view key, int slot) const;

    /** Key -> text, for one language. */
    using Phrases = StringMap<std::string>;

    StringMap<Phrases> _translations;  ///< by language code
    std::string _activeLang = "en";
    PlayerLanguages& _languages;
};

}  // namespace VoltMod
