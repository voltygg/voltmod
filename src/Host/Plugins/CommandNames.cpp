#include "Host/Plugins/CommandNames.hpp"

#include <algorithm>

namespace VoltMod
{

bool CommandNames::Register(OwnerId owner, std::string_view ownerName, std::string_view name)
{
    if (name.empty())
        return false;

    const auto found = std::ranges::find(_entries, name, &Entry::Name);
    if (found != _entries.end())
        return found->Owner == owner;

    _entries.push_back({.Name = std::string(name), .Owner = owner, .OwnerName = std::string(ownerName)});
    return true;
}

void CommandNames::RegisterForHost(std::string_view name)
{
    if (!name.empty() && std::ranges::find(_entries, name, &Entry::Name) == _entries.end())
        _entries.push_back({.Name = std::string(name), .Owner = nullptr, .OwnerName = "the host"});
}

std::string_view CommandNames::OwnerOf(std::string_view name) const
{
    const auto found = std::ranges::find(_entries, name, &Entry::Name);
    return found != _entries.end() ? std::string_view(found->OwnerName) : std::string_view();
}

void CommandNames::RemoveAll(OwnerId owner)
{
    std::erase_if(_entries, [owner](const Entry& entry) { return entry.Owner == owner; });
}

}  // namespace VoltMod
