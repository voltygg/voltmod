#include "Hooks/ClientCvarPending.hpp"

#include <algorithm>
#include <utility>

namespace VoltMod::Hooks
{

void ClientCvarPendingTable::Prune(int slot, double now)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& queries = _slots[slot];
    std::erase_if(queries, [&](const PendingCvarQuery& query) { return now - query.SentAtSec >= TimeoutSec; });
}

bool ClientCvarPendingTable::Retarget(int slot, std::string_view name, ClientCvars::QueryCallback& callback)
{
    if (!Core::IsValidSlot(slot))
        return false;

    for (PendingCvarQuery& query : _slots[slot])
    {
        if (query.Name == name)
        {
            query.Callback = std::move(callback);
            return true;
        }
    }
    return false;
}

bool ClientCvarPendingTable::Full(int slot) const
{
    return Count(slot) >= MaxPendingPerSlot;
}

int ClientCvarPendingTable::NextCookie(int slot)
{
    if (!Core::IsValidSlot(slot))
        return -1;

    const auto& queries = _slots[slot];
    // One more attempt than the cap guarantees a free cookie exists whenever the slot has room.
    for (size_t attempt = 0; attempt <= MaxPendingPerSlot; ++attempt)
    {
        if (++_cookieCounter > MaxCookie)
            _cookieCounter = 1;

        const int cookie = static_cast<int>(_cookieCounter);
        if (std::none_of(queries.begin(), queries.end(),
                         [&](const PendingCvarQuery& query) { return query.Cookie == cookie; }))
            return cookie;
    }
    return -1;
}

void ClientCvarPendingTable::Add(int slot, int cookie, std::string name, ClientCvars::QueryCallback callback,
                                 double now)
{
    if (!Core::IsValidSlot(slot) || cookie < 0)
        return;

    auto& queries = _slots[slot];
    PendingCvarQuery query{
        .Name = std::move(name), .Callback = std::move(callback), .SentAtSec = now, .Cookie = cookie};

    auto existing = std::find_if(queries.begin(), queries.end(),
                                 [&](const PendingCvarQuery& stored) { return stored.Cookie == cookie; });
    if (existing != queries.end())
        *existing = std::move(query);
    else
        queries.push_back(std::move(query));
}

std::optional<PendingCvarQuery> ClientCvarPendingTable::Take(int slot, int cookie, std::string_view name)
{
    if (!Core::IsValidSlot(slot))
        return std::nullopt;

    auto& queries = _slots[slot];
    auto it = std::find_if(queries.begin(), queries.end(),
                           [&](const PendingCvarQuery& query) { return query.Cookie == cookie; });
    if (it == queries.end() || it->Name != name)
        return std::nullopt;

    PendingCvarQuery query = std::move(*it);
    queries.erase(it);
    return query;
}

void ClientCvarPendingTable::Clear(int slot)
{
    if (Core::IsValidSlot(slot))
        _slots[slot].clear();
}

void ClientCvarPendingTable::ClearAll()
{
    for (auto& queries : _slots)
        queries.clear();
}

size_t ClientCvarPendingTable::Count(int slot) const
{
    return Core::IsValidSlot(slot) ? _slots[slot].size() : 0;
}

}  // namespace VoltMod::Hooks
