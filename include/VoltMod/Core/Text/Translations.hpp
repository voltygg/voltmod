#pragma once

#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Text/PlayerLanguages.hpp>
#include <array>
#include <functional>
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
 * dotted keys (`category.punish`). Use @ref Get(key, slot) for per-player text.
 *
 * Lookup order: the slot's language (@ref PlayerLanguage), the active language, English, the
 * framework's English defaults for its own keys (`cmd.*`, `target.*`), then the key verbatim.
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

    /** Where player languages live; must outlive this. Unset, they stay in this plugin. */
    void UseSharedLanguages(PlayerLanguages& shared);

    /** Set @p slot's language for every plugin. Empty means the active language. */
    void SetPlayerLanguage(int slot, std::string_view lang);

    /** Empty means the active language. Valid until the next change. */
    [[nodiscard]] std::string_view PlayerLanguage(int slot) const;

private:
    // Engaged (possibly with an empty view) when lang/key is present, nullopt when it is absent.
    // A view, not a string, so a lookup on the per-frame menu path copies nothing; the tables and
    // the built-in defaults both outlive the call.
    std::optional<std::string_view> LookupIn(std::string_view lang, const std::string& key) const;

    // The whole lookup chain for @p slot short of the final fallback: nullopt means nothing
    // carries the key, which is what Get and GetOr answer differently.
    std::optional<std::string_view> Resolve(const std::string& key, int slot) const;

    // Looks up a std::string key by string_view without building a string.
    struct LanguageHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view code) const { return std::hash<std::string_view>{}(code); }
    };

    std::unordered_map<std::string, std::unordered_map<std::string, std::string>, LanguageHash, std::equal_to<>>
        _translations;
    std::string _activeLang = "en";
    /** Used only when no shared table is set. */
    std::array<std::string, MaxPlayers> _playerLangs{};
    PlayerLanguages* _shared = nullptr;
    /** Declared after _playerLangs so it unregisters before the entries its callback clears. */
    Subscription _slotListener;
};

}  // namespace VoltMod
