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
 * @brief Owns a plugin's settings and republishes them whole on every reload.
 *
 * @p TSettings is a plain aggregate: Glaze reflects its public members, so no registration is
 * needed. A missing key keeps the member's C++ initializer and unknown keys are ignored.
 *
 * `Options<Settings>` publishes exactly what the file parsed into. A plugin that has to validate
 * values or derive more of them names a second type and the function that builds it:
 *
 * @code
 * struct Snapshot
 * {
 *     Settings Values;
 *     std::vector<int> MenuDurationSecs;
 * };
 * static Snapshot BuildSnapshot(Settings raw);
 *
 * VoltMod::Options<Settings, Snapshot> options{&BuildSnapshot};
 * @endcode
 *
 * The snapshot is built before it is published, so a reload that fails to parse leaves the
 * previous one intact and nothing ever observes a half-validated configuration.
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
