#include "Hooks/PendingConVarQueries.hpp"

#include <algorithm>
#include <utility>

namespace VoltMod
{

void PendingConVarQueries::Prune(int slot, double now)
{
    if (!IsValidSlot(slot))
        return;

    auto& queries = _slots[slot];
    std::erase_if(queries, [&](const PendingConVarQuery& query) { return now - query.SentAtSec >= TimeoutSec; });
}

bool PendingConVarQueries::Retarget(int slot, std::string_view name, ClientConVars::QueryCallback& callback)
{
    if (!IsValidSlot(slot))
        return false;

    for (PendingConVarQuery& query : _slots[slot])
    {
        if (query.Name == name)
        {
            query.Callback = std::move(callback);
            return true;
        }
    }
    return false;
}

bool PendingConVarQueries::Full(int slot) const
{
    return Count(slot) >= MaxPendingPerSlot;
}

int PendingConVarQueries::NextCookie(int slot)
{
    if (!IsValidSlot(slot))
        return -1;

    const auto& queries = _slots[slot];
    // One more attempt than the cap guarantees a free cookie exists whenever the slot has room.
    for (size_t attempt = 0; attempt <= MaxPendingPerSlot; ++attempt)
    {
        if (++_cookieCounter > MaxCookie)
            _cookieCounter = 1;

        const int cookie = static_cast<int>(_cookieCounter);
        if (std::none_of(queries.begin(), queries.end(),
                         [&](const PendingConVarQuery& query) { return query.Cookie == cookie; }))
            return cookie;
    }
    return -1;
}

void PendingConVarQueries::Add(int slot, int cookie, std::string name, ClientConVars::QueryCallback callback,
                                 double now)
{
    if (!IsValidSlot(slot) || cookie < 0)
        return;

    auto& queries = _slots[slot];
    PendingConVarQuery query{
        .Name = std::move(name), .Callback = std::move(callback), .SentAtSec = now, .Cookie = cookie};

    auto existing = std::find_if(queries.begin(), queries.end(),
                                 [&](const PendingConVarQuery& stored) { return stored.Cookie == cookie; });
    if (existing != queries.end())
        *existing = std::move(query);
    else
        queries.push_back(std::move(query));
}

std::optional<PendingConVarQuery> PendingConVarQueries::Take(int slot, int cookie, std::string_view name)
{
    if (!IsValidSlot(slot))
        return std::nullopt;

    auto& queries = _slots[slot];
    auto it = std::find_if(queries.begin(), queries.end(),
                           [&](const PendingConVarQuery& query) { return query.Cookie == cookie; });
    if (it == queries.end() || it->Name != name)
        return std::nullopt;

    PendingConVarQuery query = std::move(*it);
    queries.erase(it);
    return query;
}

void PendingConVarQueries::Clear(int slot)
{
    if (IsValidSlot(slot))
        _slots[slot].clear();
}

void PendingConVarQueries::ClearAll()
{
    for (auto& queries : _slots)
        queries.clear();
}

size_t PendingConVarQueries::Count(int slot) const
{
    return IsValidSlot(slot) ? _slots[slot].size() : 0;
}

}  // namespace VoltMod
