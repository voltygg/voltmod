#pragma once

#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <array>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
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
    /** @p slots tells the table when a slot changes hands, so one player's language pick
     *  cannot answer for the next occupant. */
    explicit Translations(SlotEvents& slots);

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
    std::string Get(std::string_view key, int slot, const std::map<std::string, std::string>& tokens) const;

    /** Active-language variant of the token-substituting @ref Get. */
    std::string Get(std::string_view key, const std::map<std::string, std::string>& tokens) const;

    /** Language codes that were successfully loaded (one per JSON file). */
    std::vector<std::string> GetAvailableLanguages() const;

    /** Set/clear a slot's preferred language. Empty (or cleared) means "use the active language". */
    void SetPlayerLanguage(int slot, std::string_view lang);
    void ClearPlayerLanguage(int slot);

private:
    // Engaged (possibly with an empty view) when lang/key is present, nullopt when it is absent.
    // A view, not a string, so a lookup on the per-frame menu path copies nothing; the tables and
    // the built-in defaults both outlive the call.
    std::optional<std::string_view> LookupIn(const std::string& lang, const std::string& key) const;

    // The whole lookup chain for @p slot short of the final fallback: nullopt means nothing
    // carries the key, which is what Get and GetOr answer differently.
    std::optional<std::string_view> Resolve(const std::string& key, int slot) const;

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> _translations;
    std::string _activeLang = "en";
    std::array<std::string, MaxPlayers> _playerLangs{};
    /** Declared after _playerLangs so it unregisters before the entries its callback clears. */
    Subscription _slotListener;
};

}  // namespace VoltMod
