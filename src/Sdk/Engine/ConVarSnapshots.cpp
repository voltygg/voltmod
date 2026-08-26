#include <VoltMod/Sdk/Engine/ConVarSnapshots.hpp>
#include <algorithm>

namespace VoltMod::Sdk
{

bool ConVarSnapshots::Save(std::string_view name, std::string_view value)
{
    if (name.empty() || Contains(name))
        return false;

    _entries.push_back({.Name = std::string(name), .Value = std::string(value)});
    return true;
}

std::optional<std::string> ConVarSnapshots::Remove(std::string_view name)
{
    auto it = Find(name);
    if (it == _entries.end())
        return std::nullopt;

    auto value = it->Value;
    _entries.erase(it);
    return value;
}

bool ConVarSnapshots::Contains(std::string_view name) const
{
    return Find(name) != _entries.end();
}

std::vector<ConVarSnapshots::Entry>::const_iterator ConVarSnapshots::Find(std::string_view name) const
{
    return std::ranges::find_if(_entries, [name](const Entry& entry) { return entry.Name == name; });
}

}  // namespace VoltMod::Sdk
