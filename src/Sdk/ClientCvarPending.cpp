#include <CS2Kit/Sdk/Detail/ClientCvarPending.hpp>
#include <utility>

namespace CS2Kit::Sdk::Detail
{

void ClientCvarPendingTable::Prune(int slot, double now)
{
    if (!Core::IsValidSlot(slot))
        return;

    auto& queries = _slots[slot];
    for (auto it = queries.begin(); it != queries.end();)
    {
        if (now - it->second.SentAtSec >= TimeoutSec)
            it = queries.erase(it);
        else
            ++it;
    }
}

bool ClientCvarPendingTable::Retarget(int slot, std::string_view name, ClientCvarService::QueryCallback& callback)
{
    if (!Core::IsValidSlot(slot))
        return false;

    for (auto& [cookie, query] : _slots[slot])
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
        if (!queries.contains(cookie))
            return cookie;
    }
    return -1;
}

void ClientCvarPendingTable::Add(int slot, int cookie, std::string name, ClientCvarService::QueryCallback callback,
                                 double now)
{
    if (!Core::IsValidSlot(slot) || cookie < 0)
        return;

    _slots[slot].insert_or_assign(cookie, PendingCvarQuery{std::move(name), std::move(callback), now});
}

std::optional<PendingCvarQuery> ClientCvarPendingTable::Take(int slot, int cookie, std::string_view name)
{
    if (!Core::IsValidSlot(slot))
        return std::nullopt;

    auto& queries = _slots[slot];
    auto it = queries.find(cookie);
    if (it == queries.end() || it->second.Name != name)
        return std::nullopt;

    PendingCvarQuery query = std::move(it->second);
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

}  // namespace CS2Kit::Sdk::Detail
