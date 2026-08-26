#include <VoltMod/Sdk/Engine/ConVarOverrides.hpp>
#include <VoltMod/Sdk/Engine/ConVarService.hpp>
#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

namespace VoltMod::Sdk
{

void ConVarOverrides::Take(std::string_view name, std::string_view value, std::string_view fallback)
{
    if (name.empty())
        return;

    // Snapshot only on the first take. A re-take is a re-assert (see the header on map changes),
    // and re-reading there would save the override as if it were the operator's own value.
    if (!Holds(name))
    {
        std::string owned{name};
        auto saved = _conVars.GetString(owned.c_str()).value_or(std::string(fallback));
        _held.push_back({.Name = std::move(owned), .Saved = std::move(saved)});
    }

    Write(name, value);
}

void ConVarOverrides::Take(std::string_view name, float value, float fallback)
{
    Take(name, std::format("{}", value), std::format("{}", fallback));
}

void ConVarOverrides::Release(std::string_view name)
{
    auto index = IndexOf(name);
    if (!index)
        return;

    const auto& entry = _held[*index];
    Write(entry.Name, entry.Saved);
    _held.erase(_held.begin() + static_cast<std::ptrdiff_t>(*index));
}

void ConVarOverrides::ReleaseAll()
{
    for (const auto& entry : _held)
        Write(entry.Name, entry.Saved);
    _held.clear();
}

std::optional<std::size_t> ConVarOverrides::IndexOf(std::string_view name) const
{
    auto it = std::ranges::find_if(_held, [name](const Entry& e) { return e.Name == name; });
    if (it == _held.end())
        return std::nullopt;
    return static_cast<std::size_t>(std::ranges::distance(_held.begin(), it));
}

void ConVarOverrides::Write(std::string_view name, std::string_view value)
{
    _conVars.ExecuteServerCommand(std::format("{} {}", name, value).c_str());
}

}  // namespace VoltMod::Sdk
