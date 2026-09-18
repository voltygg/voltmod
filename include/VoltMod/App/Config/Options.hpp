#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <concepts>
#include <functional>
#include <string_view>
#include <utility>

namespace VoltMod
{

/**
 * @brief Owns a plugin's settings and republishes them whole on every Load.
 *
 * @p TSettings is a plain aggregate whose member names are the JSON keys. A missing key keeps the
 * member's initializer and unknown keys are ignored. To validate or derive values, name a
 * @p TSnapshot and pass the function that builds it from the parsed settings. The snapshot is
 * built before it is published, so a failed Load keeps the previous one. See docs/config.md.
 */
template <class TSettings, class TSnapshot = TSettings>
class Options
{
public:
    /** @brief Validates the parsed settings and derives whatever Get() offers beside them. */
    using Builder = std::function<TSnapshot(TSettings)>;

    /** Publishes the parsed settings unchanged. */
    Options()
        requires std::same_as<TSettings, TSnapshot>
        : _build([](TSettings parsed) { return parsed; })
    {}

    explicit Options(Builder build) : _build(std::move(build)) {}

    /** @brief Load @p path (JSONC tolerated), build the snapshot, then publish it in one move.
     *
     *  On failure the previously published snapshot stands, and the error names the offending
     *  key with its line and column. */
    Status Load(std::string_view path)
    {
        auto parsed = Json::ReadFile<TSettings>(path);
        if (!parsed)
            return std::unexpected(parsed.error());

        TSnapshot next = _build(std::move(*parsed));
        _snapshot = std::move(next);
        Log::Info("Loaded settings from {}", path);
        return {};
    }

    /** The effective settings. */
    const TSnapshot& Get() const { return _snapshot; }

private:
    Builder _build;
    TSnapshot _snapshot{};
};

}  // namespace VoltMod
