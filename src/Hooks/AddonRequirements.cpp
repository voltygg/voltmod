#include "Hooks/AddonRequirements.hpp"

#include <algorithm>
#include <charconv>

namespace VoltMod
{

bool AddonRequirements::Take(std::vector<Requirement>& list, uint64_t id)
{
    if (id == 0)
        return false;

    const auto found = std::ranges::find(list, id, &Requirement::Id);
    if (found != list.end())
        ++found->Refs;
    else
        list.push_back({.Id = id, .Refs = 1});
    return true;
}

void AddonRequirements::Drop(std::vector<Requirement>& list, uint64_t id)
{
    const auto found = std::ranges::find(list, id, &Requirement::Id);
    if (found == list.end())
        return;

    if (--found->Refs <= 0)
        list.erase(found);
}

bool AddonRequirements::Require(uint64_t id)
{
    return Take(_global, id);
}

void AddonRequirements::Release(uint64_t id)
{
    Drop(_global, id);
}

bool AddonRequirements::RequireFor(int64_t steamId, uint64_t id)
{
    return Take(_clients[steamId].Extra, id);
}

void AddonRequirements::ReleaseFor(int64_t steamId, uint64_t id)
{
    const auto found = _clients.find(steamId);
    if (found != _clients.end())
        Drop(found->second.Extra, id);
}

bool AddonRequirements::Empty() const
{
    return _global.empty() && std::ranges::all_of(_clients, [](const auto& e) { return e.second.Extra.empty(); });
}

std::vector<uint64_t> AddonRequirements::Required() const
{
    std::vector<uint64_t> ids;
    ids.reserve(_global.size());
    for (const Requirement& requirement : _global)
        ids.push_back(requirement.Id);
    return ids;
}

std::vector<uint64_t> AddonRequirements::MissingFor(int64_t steamId) const
{
    std::vector<uint64_t> missing = Required();

    if (const auto found = _clients.find(steamId); found != _clients.end())
    {
        for (const Requirement& extra : found->second.Extra)
            if (!std::ranges::contains(missing, extra.Id))
                missing.push_back(extra.Id);

        for (uint64_t downloaded : found->second.Downloaded)
            std::erase(missing, downloaded);
    }

    return missing;
}

AddonDecision AddonRequirements::NextFor(int64_t steamId, double now, int maxAttempts)
{
    const std::vector<uint64_t> missing = MissingFor(steamId);
    if (missing.empty())
        return {};

    ClientState& state = _clients[steamId];
    const uint64_t next = missing.front();

    // The same addon coming round again means the last offer was not taken.
    state.Attempts = (state.Sending == next) ? state.Attempts + 1 : 1;
    if (state.Attempts > maxAttempts)
        return {.Step = AddonStep::GiveUp, .Id = next};

    state.Sending = next;
    state.SentAt = now;
    return {.Step = AddonStep::Send, .Id = next};
}

void AddonRequirements::NoteInFlight(int64_t steamId, uint64_t id, double now)
{
    if (id == 0)
        return;

    ClientState& state = _clients[steamId];
    state.Sending = id;
    state.SentAt = now;
    state.Attempts = 0;
}

void AddonRequirements::CreditReconnect(int64_t steamId, double now, double timeoutSec)
{
    const auto found = _clients.find(steamId);
    if (found == _clients.end())
        return;

    ClientState& state = found->second;
    if (state.Sending == 0)
        return;

    if (now - state.SentAt <= timeoutSec)
    {
        if (!std::ranges::contains(state.Downloaded, state.Sending))
            state.Downloaded.push_back(state.Sending);
        state.Attempts = 0;
    }
    state.Sending = 0;
}

void AddonRequirements::ForgetClients()
{
    // Per-client requirements are requirements, not progress: keep them and clear the rest.
    for (auto it = _clients.begin(); it != _clients.end();)
    {
        if (it->second.Extra.empty())
        {
            it = _clients.erase(it);
            continue;
        }

        it->second.Downloaded.clear();
        it->second.Sending = 0;
        it->second.SentAt = 0.0;
        it->second.Attempts = 0;
        ++it;
    }
}

std::vector<uint64_t> ParseAddonList(std::string_view field)
{
    std::vector<uint64_t> ids;

    while (!field.empty())
    {
        const size_t comma = field.find(',');
        const std::string_view token = field.substr(0, comma);

        uint64_t id = 0;
        const auto* end = token.data() + token.size();
        if (auto [stop, ec] = std::from_chars(token.data(), end, id); ec == std::errc{} && stop == end && id != 0)
            ids.push_back(id);

        if (comma == std::string_view::npos)
            break;
        field.remove_prefix(comma + 1);
    }

    return ids;
}

}  // namespace VoltMod
