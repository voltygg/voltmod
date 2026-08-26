#pragma once

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <concepts>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod::Validation
{

/**
 * @brief Declarative helpers for post-load config validation: skip bad entries with a
 * warning instead of failing the whole load, and fall back to sane defaults.
 */

/** Reset @p value to @p fallback (warning names @p what) when it is blank or longer than @p maxLen. */
inline void NormalizeTag(std::string& value, std::size_t maxLen, std::string_view fallback, std::string_view what)
{
    if (!Strings::Trim(value).empty() && value.size() <= maxLen)
        return;

    Log::Warn("settings: {} is empty or longer than {} chars; using \"{}\"", what, maxLen, fallback);
    value = std::string(fallback);
}

/** Keep only items where @p validate(item, index) returns nullopt; each rejection logs its reason.
 *  @p validate: `std::optional<std::string>(const T&, std::size_t index)`. */
template <class T, class Fn>
void FilterValid(std::vector<T>& items, Fn&& validate, std::string_view what)
{
    std::vector<T> kept;
    kept.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i)
    {
        if (auto reason = validate(items[i], i))
            Log::Warn("settings: skipping {}[{}]: {}", what, i, *reason);
        else
            kept.push_back(std::move(items[i]));
    }
    items = std::move(kept);
}

/** Restore @p defaults (warning names @p what) when filtering left @p items empty, so a list that
 *  would dead-end a menu falls back instead. Returns true when the defaults were applied. */
template <class T>
bool FallbackIfEmpty(std::vector<T>& items, const std::vector<T>& defaults, std::string_view what)
{
    if (!items.empty() || defaults.empty())
        return false;

    Log::Warn("settings: {} has no valid entries; using built-in defaults", what);
    items = defaults;
    return true;
}

/** As above, but @p makeDefaults is only called when the fallback is actually needed, and its
 *  result is moved rather than copied. Prefer this when building the defaults costs anything -
 *  passing them by value forces every caller to build a list it almost always discards.
 *  @p makeDefaults: `std::vector<T>()`. */
template <class T, std::invocable Fn>
bool FallbackIfEmpty(std::vector<T>& items, Fn&& makeDefaults, std::string_view what)
{
    if (!items.empty())
        return false;

    auto defaults = makeDefaults();
    if (defaults.empty())
        return false;

    Log::Warn("settings: {} has no valid entries; using built-in defaults", what);
    items = std::move(defaults);
    return true;
}

/** Parse @ref ParseDuration strings, dropping invalid entries with warnings. Falls back to
 *  parsing @p defaults when nothing valid remains (so the list exists in exactly one place). */
inline std::vector<int> ParseDurations(const std::vector<std::string>& entries,
                                       const std::vector<std::string>& defaults, std::string_view what)
{
    std::vector<std::string> kept = entries;
    FilterValid(
        kept,
        [](const std::string& entry, std::size_t) -> std::optional<std::string> {
            if (ParseDuration(entry) < 0)
                return std::format("bad duration '{}'", entry);
            return std::nullopt;
        },
        what);
    FallbackIfEmpty(kept, defaults, what);

    std::vector<int> result;
    result.reserve(kept.size());
    for (const auto& entry : kept)
        result.push_back(ParseDuration(entry));
    return result;
}

}  // namespace VoltMod::Validation
