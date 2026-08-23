#pragma once

#include <CS2Kit/Utils/Json.hpp>
#include <string>

namespace CS2Kit::App
{

/**
 * @brief Owns one JSON-deserialized settings struct.
 *
 * @p TSettings needs nlohmann `to_json`/`from_json` - easiest via
 * `NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT`, which defaults any missing key.
 * Subclass it to add domain accessors and post-load validation:
 *
 * @code
 * class ConfigManager : public CS2Kit::JsonConfig<Settings>
 * {
 * public:
 *     bool LoadSettings(const std::string& path) { return Load(path) && (Resolve(), true); }
 *     const ServerSettings& GetServer() const { return Get().server; }
 * };
 * @endcode
 */
template <class TSettings>
class JsonConfig
{
public:
    /** Load + deserialize @p path (JSONC tolerated). Logs and returns false on a missing file,
     *  parse error, or wrong-typed value. */
    bool Load(const std::string& path)
    {
        auto loaded = Utils::Json::TryDeserializeFile<TSettings>(path);
        if (!loaded)
            return false;

        _settings = std::move(*loaded);
        Utils::Log::Info("Loaded settings from {}", path);
        return true;
    }

    const TSettings& Get() const { return _settings; }

    /** Mutable access for post-load validation fixups. */
    TSettings& Mutable() { return _settings; }

private:
    TSettings _settings;
};

}  // namespace CS2Kit::App
