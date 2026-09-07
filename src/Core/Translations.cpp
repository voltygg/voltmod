#include <VoltMod/Core/File.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <filesystem>
#include <string>
#include <string_view>

namespace VoltMod
{

// Flatten nested JSON objects into dotted keys and keep only string leaves.
static void FlattenInto(const glz::generic& node, const std::string& prefix,
                        std::unordered_map<std::string, std::string>& out)
{
    if (!node.is_object())
        return;

    for (const auto& [key, value] : node.get_object())
    {
        std::string full = prefix.empty() ? key : prefix + "." + key;
        if (value.is_object())
            FlattenInto(value, full, out);
        else if (value.is_string())
            out[full] = value.get_string();
    }
}

/**
 * English text for the keys the framework itself emits. Translations::Get returns the raw key on a
 * miss, so without this every plugin had to hand-copy all of them into every language file or
 * players saw the literal string `cmd.noPermission`. A plugin's own file still wins - these are
 * the floor, not an override.
 */
static const std::unordered_map<std::string, std::string>& KitDefaults()
{
    static const std::unordered_map<std::string, std::string> defaults{
        {"cmd.noPermission", "You do not have permission to use this command."},
        {"cmd.tooManyArgs", "Too many arguments. Usage: {usage}"},
        {"cmd.usage", "Usage: {usage}"},
        {"cmd.usage.target", "target"},
        {"cmd.usage.targets", "targets"},
        {"cmd.usage.duration", "duration"},
        {"cmd.usage.steamId", "steamId"},
        {"cmd.usage.playerOrSteamId", "target|steamId"},
        {"cmd.usage.int", "number"},
        {"cmd.usage.u64", "id"},
        {"cmd.usage.word", "value"},
        {"cmd.usage.rest", "reason"},
        {"cmd.badDuration", "Invalid duration. Use minutes (e.g. 30), 30s/5m/2h/7d, or 'perm'."},
        {"cmd.badSteamId", "'{token}' is not a valid SteamID64."},
        {"cmd.badNumber", "'{token}' is not a valid number."},
        {"target.noMatch", "No player matches '{token}'."},
        {"target.immune", "'{token}' is immune to that."},
        {"target.ambiguous", "'{token}' matches {count} players - be more specific."},
        {"target.dead", "'{token}' is not alive."},
        {"target.bot", "'{token}' is a bot."},
        {"menu.stepFailed", "That menu could not be opened."},
        {"effectState.on", "ON"},
        {"effectState.off", "OFF"},
    };
    return defaults;
}

Translations::Translations(SlotEvents& slots)
    // SlotEvents covers both arrival and departure; arrival happens before OnPlayerConnect sets
    // the language.
    : _slotListener(slots.Changed += [this](int slot) { ClearPlayerLanguage(slot); })
{}

bool Translations::Load(std::string_view dirPath)
{
    _translations.clear();
    namespace fs = std::filesystem;

    auto resolvedPath = ResolvePath(dirPath);

    if (!fs::exists(resolvedPath) || !fs::is_directory(resolvedPath))
    {
        Log::Warn("Translations directory not found: {}", resolvedPath.string());
        return false;
    }

    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(resolvedPath))
    {
        if (entry.path().extension() != ".json")
            continue;

        std::string langCode = entry.path().stem().string();
        auto text = ReadAllText(entry.path().string());
        if (!text)
            continue;

        // Language files remain strict JSON, not JSONC.
        auto data = Json::ParseDocument(*text);
        if (!data)
        {
            // Skip one invalid language without discarding the others.
            Log::Warn("Failed to parse {}: {}", entry.path().string(), data.error().Detail);
            continue;
        }

        auto& langMap = _translations[langCode];
        FlattenInto(*data, "", langMap);

        ++loaded;
        Log::Info("Loaded translations: {} ({} keys)", langCode, langMap.size());
    }

    Log::Info("Loaded {} language(s).", loaded);

    // Report missing keys at load time, using "en" as the reference when present.
    if (auto en = _translations.find("en"); en != _translations.end() && _translations.size() > 1)
    {
        for (const auto& [code, keys] : _translations)
        {
            if (code == "en")
            {
                continue;
            }

            size_t missing = 0;
            for (const auto& [key, value] : en->second)
            {
                if (!keys.contains(key))
                {
                    ++missing;
                }
            }

            if (missing > 0)
            {
                Log::Warn("Translations: '{}' is missing {} key(s) present in en.", code, missing);
            }
        }
    }

    return loaded > 0;
}

void Translations::SetLanguage(std::string_view lang)
{
    _activeLang = lang;
}

const std::string& Translations::GetLanguage() const
{
    return _activeLang;
}

std::vector<std::string> Translations::GetAvailableLanguages() const
{
    std::vector<std::string> langs;
    langs.reserve(_translations.size());
    for (const auto& [code, _] : _translations)
    {
        langs.push_back(code);
    }

    return langs;
}

void Translations::SetPlayerLanguage(int slot, std::string_view lang)
{
    if (IsValidSlot(slot))
    {
        _playerLangs[slot] = lang;
    }
}

void Translations::ClearPlayerLanguage(int slot)
{
    if (IsValidSlot(slot))
    {
        _playerLangs[slot].clear();
    }
}

std::optional<std::string_view> Translations::LookupIn(const std::string& lang, const std::string& key) const
{
    auto langIt = _translations.find(lang);
    if (langIt != _translations.end())
    {
        auto keyIt = langIt->second.find(key);
        if (keyIt != langIt->second.end())
            return keyIt->second;
    }
    return std::nullopt;
}

std::string Translations::Get(std::string_view key) const
{
    return Get(key, -1);  // negative slot uses the active language
}

std::optional<std::string_view> Translations::Resolve(const std::string& key, int slot) const
{
    const std::string& lang = (IsValidSlot(slot) && !_playerLangs[slot].empty()) ? _playerLangs[slot] : _activeLang;
    static const std::string kEnglish = "en";

    for (const std::string* candidate : {&lang, &_activeLang, &kEnglish})
        if (auto v = LookupIn(*candidate, key))
            return v;

    if (auto it = KitDefaults().find(key); it != KitDefaults().end())
        return it->second;
    return std::nullopt;
}

std::string Translations::Get(std::string_view key, int slot) const
{
    // The tables are keyed by std::string, so the view is materialized once for the lookup.
    const std::string name(key);
    auto value = Resolve(name, slot);
    return value ? std::string(*value) : name;
}

std::string Translations::GetOr(std::string_view key, int slot, std::string_view fallback) const
{
    auto value = Resolve(std::string(key), slot);
    return std::string(value.value_or(fallback));
}

std::string Translations::Get(std::string_view key, int slot, const std::map<std::string, std::string>& tokens) const
{
    return Strings::SubstituteTokens(Get(key, slot), tokens);
}

std::string Translations::Get(std::string_view key, const std::map<std::string, std::string>& tokens) const
{
    return Get(key, -1, tokens);
}

}  // namespace VoltMod
