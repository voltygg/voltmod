#include "Host/Plugins/ServiceTable.hpp"

#include <algorithm>
#include <utility>

namespace VoltMod
{

void ServiceTable::Publish(OwnerId owner, std::string_view name, void* implementation)
{
    if (name.empty() || implementation == nullptr)
        return;

    const auto held = std::ranges::find(_services, name, &Service::Name);
    if (held == _services.end())
        _services.push_back({.Name = std::string(name), .Implementation = implementation, .Owner = owner});
    else if (held->Owner == owner)
        held->Implementation = implementation;
    else
        return;

    RaiseChanged(name, true);
}

void ServiceTable::Unpublish(OwnerId owner, std::string_view name)
{
    const auto held = std::ranges::find(_services, name, &Service::Name);
    if (held == _services.end() || held->Owner != owner)
        return;

    const std::string withdrawn = std::move(held->Name);
    _services.erase(held);
    RaiseChanged(withdrawn, false);
}

void* ServiceTable::Find(std::string_view name) const
{
    const auto held = std::ranges::find(_services, name, &Service::Name);
    return held != _services.end() ? held->Implementation : nullptr;
}

std::vector<std::string> ServiceTable::RemoveAll(OwnerId owner)
{
    std::vector<std::string> withdrawn;
    for (const Service& service : _services)
        if (service.Owner == owner)
            withdrawn.push_back(service.Name);

    std::erase_if(_services, [owner](const Service& service) { return service.Owner == owner; });

    // Raise once the table is settled: a Changed callback may publish or withdraw in turn.
    for (const std::string& name : withdrawn)
        RaiseChanged(name, false);

    return withdrawn;
}

void ServiceTable::NotifyPublished(IHostServices::ChangedFn callback, void* context) const
{
    // Copy the names out first: a replayed callback may publish or withdraw as it goes.
    std::vector<std::string> published;
    published.reserve(_services.size());
    for (const Service& service : _services)
        published.push_back(service.Name);

    for (const std::string& name : published)
        callback(context, name, true);
}

void ServiceTable::RaiseChanged(std::string_view name, bool published)
{
    const std::string_view changed = name;
    _changed.Dispatch([&](IHostServices::ChangedFn callback, void* context) {
        callback(context, changed, published);
        return false;
    });
}

}  // namespace VoltMod
