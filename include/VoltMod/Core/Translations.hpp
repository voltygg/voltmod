#pragma once

#include <array>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace VoltMod::Core
{

/** `{token}` -> replacement map for the token-substituting @ref Translations::Get overloads. */
using Tokens = std::map<std::string, std::string>;

/**
 * @brief Localization system. Loads one JSON file per language; nested objects flatten into
 * dotted keys (`category.punish`). Use @ref Get(key, slot) for per-player text; use
 * @ref SetPlayerLanguage to register a slot's preferred language.
 *
 * Lookup order is the slot's language, then the active language, then English, then the framework's
 * own English defaults for the keys it emits itself (`cmd.*`, `target.*`), then the key
 * verbatim. A plugin that translates those keys still wins; one that forgets a language gets
 * readable English instead of a raw key on screen.
 */
class Translations
{
public:
    static constexpr int MaxSlots = 64;

    Translations() = default;

    bool Load(const std::string& dirPath);
    void SetLanguage(const std::string& lang);
    const std::string& GetLanguage() const;

    /** Look up a key in the active (server) language, falling back to English. For broadcasts;
     *  prefer @ref Get(key, slot) for any message addressed to a specific player. */
    std::string Get(const std::string& key) const;

    /** Look up a key in @p slot's registered language, falling back to the active language then English. */
    std::string Get(const std::string& key, int slot) const;

    /** @ref Get(key, slot), then replace each `{token}` occurrence from @p tokens. */
    std::string Get(const std::string& key, int slot, const std::map<std::string, std::string>& tokens) const;

    /** Active-language variant of the token-substituting @ref Get. */
    std::string Get(const std::string& key, const std::map<std::string, std::string>& tokens) const;

    /** Language codes that were successfully loaded (one per JSON file). */
    std::vector<std::string> GetAvailableLanguages() const;

    /** Set/clear a slot's preferred language. Empty (or cleared) means "use the active language". */
    void SetPlayerLanguage(int slot, const std::string& lang);
    void ClearPlayerLanguage(int slot);

private:
    // Returns a pointer to the stored value (which may be empty), or nullptr when lang/key is absent.
    const std::string* LookupIn(const std::string& lang, const std::string& key) const;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _translations;
    std::string _activeLang = "en";
    std::array<std::string, MaxSlots> _playerLangs{};
};

}  // namespace VoltMod::Core
