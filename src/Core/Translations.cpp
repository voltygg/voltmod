#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace VoltMod
{

// Flatten nested objects into dotted keys (`category.punish`), so callers can group keys in
// the JSON without changing the flat lookup model. Leaf string values are stored; non-string
// leaves are ignored.
static void FlattenInto(const nlohmann::json& node, const std::string& prefix,
                        std::unordered_map<std::string, std::string>& out)
{
    for (auto& [key, value] : node.items())
    {
        std::string full = prefix.empty() ? key : prefix + "." + key;
        if (value.is_object())
            FlattenInto(value, full, out);
        else if (value.is_string())
            out[full] = value.get<std::string>();
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
        {"cmd.badDuration", "Invalid duration. Use minutes (e.g. 30), 30s/5m/2h/7d, or 'perm'."},
        {"cmd.badSteamId", "'{token}' is not a valid SteamID64."},
        {"cmd.badNumber", "'{token}' is not a valid number."},
        {"target.noMatch", "No player matches '{token}'."},
        {"target.immune", "'{token}' is immune to that."},
        {"target.ambiguous", "'{token}' matches {count} players - be more specific."},
        {"target.dead", "'{token}' is not alive."},
        {"target.bot", "'{token}' is a bot."},
    };
    return defaults;
}

Translations::Translations(SlotEvents& slots)
    // SlotEvents is raised from AddPlayer as well as RemovePlayer, but AddPlayer raises it
    // before the plugin's OnPlayerConnect runs, which is where a language gets set - so clearing
    // on arrival drops nothing, and one event covers both edges.
    : _slotListener(slots.Changed += [this](int slot) { ClearPlayerLanguage(slot); })
{}

bool Translations::Load(const std::string& dirPath)
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
        try
        {
            std::ifstream file(entry.path());
            if (!file.is_open())
                continue;

            auto data = nlohmann::json::parse(file);
            auto& langMap = _translations[langCode];
            FlattenInto(data, "", langMap);

            ++loaded;
            Log::Info("Loaded translations: {} ({} keys)", langCode, langMap.size());
        }
        catch (const std::exception& e)
        {
            Log::Warn("Failed to parse {}: {}", entry.path().string(), e.what());
        }
    }

    Log::Info("Loaded {} language(s).", loaded);

    // Surface per-language key gaps at load time, rather than silently rendering the raw key at runtime.
    // "en" is the reference set when present.
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

void Translations::SetLanguage(const std::string& lang)
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

void Translations::SetPlayerLanguage(int slot, const std::string& lang)
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

const std::string* Translations::LookupIn(const std::string& lang, const std::string& key) const
{
    auto langIt = _translations.find(lang);
    if (langIt != _translations.end())
    {
        auto keyIt = langIt->second.find(key);
        if (keyIt != langIt->second.end())
            return &keyIt->second;
    }
    return nullptr;
}

std::string Translations::Get(const std::string& key) const
{
    return Get(key, -1);  // negative slot skips the per-player lookup, resolving against the active language
}

std::string Translations::Get(const std::string& key, int slot) const
{
    const std::string& lang = (IsValidSlot(slot) && !_playerLangs[slot].empty()) ? _playerLangs[slot] : _activeLang;

    // Pointer (not empty-string) sentinel so a key deliberately mapped to "" is honored, not dropped.
    if (const std::string* v = LookupIn(lang, key))
        return *v;
    if (lang != "en")
        if (const std::string* v = LookupIn("en", key))
            return *v;
    if (auto it = KitDefaults().find(key); it != KitDefaults().end())
        return it->second;
    return key;
}

std::string Translations::Get(const std::string& key, int slot, const std::map<std::string, std::string>& tokens) const
{
    return Strings::SubstituteTokens(Get(key, slot), tokens);
}

std::string Translations::Get(const std::string& key, const std::map<std::string, std::string>& tokens) const
{
    return Get(key, -1, tokens);
}

}  // namespace VoltMod
