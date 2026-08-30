#pragma once

#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief Owns one JSON-deserialized settings struct.
 *
 * @p TSettings is a plain aggregate: Glaze reflects its public members, so no registration is
 * needed. A missing key keeps the member's C++ initializer and unknown keys are ignored.
 *
 * Use it directly when loading is all you need. When a plugin has to validate or derive values,
 * **compose** it rather than subclassing, so nothing can observe a half-resolved configuration:
 *
 * @code
 * class ConfigManager
 * {
 * public:
 *     Status LoadSettings(std::string_view path)
 *     {
 *         auto raw = Json::ReadFile<Settings>(path);
 *         if (!raw)
 *             return std::unexpected(raw.error());
 *         _snapshot = BuildSnapshot(std::move(*raw));  // validate, then publish in one move
 *         return {};
 *     }
 *     const Settings& Get() const { return _snapshot.Values; }
 * };
 * @endcode
 */
template <class TSettings>
class JsonConfig
{
public:
    /** @brief Load and publish @p path (JSONC tolerated).
     *
     *  On failure the previously published settings stand, and the error carries a
     *  parse error. */
    Status Load(std::string_view path)
    {
        auto loaded = Json::ReadFile<TSettings>(path);
        if (!loaded)
            return std::unexpected(loaded.error());

        _settings = std::move(*loaded);
        Log::Info("Loaded settings from {}", path);
        return {};
    }

    const TSettings& Get() const { return _settings; }

private:
    TSettings _settings;
};

}  // namespace VoltMod
