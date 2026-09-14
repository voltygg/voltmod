#include "Workshop/AddonDownloads.hpp"

#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <utility>

namespace VoltMod
{

bool AddonDownloads::AddHolder(std::vector<Requirement>& list, uint64_t id)
{
    if (id == 0)
        return false;

    const auto found = std::ranges::find(list, id, &Requirement::Id);
    if (found != list.end())
        ++found->Holders;
    else
        list.push_back({.Id = id, .Holders = 1});
    return true;
}

void AddonDownloads::RemoveHolder(std::vector<Requirement>& list, uint64_t id)
{
    const auto found = std::ranges::find(list, id, &Requirement::Id);
    if (found != list.end() && --found->Holders <= 0)
        list.erase(found);
}

bool AddonDownloads::Require(uint64_t id)
{
    return AddHolder(_everyone, id);
}

void AddonDownloads::Release(uint64_t id)
{
    RemoveHolder(_everyone, id);
}

bool AddonDownloads::RequireFor(int64_t steamId, uint64_t id)
{
    return AddHolder(_clients[steamId].Required, id);
}

void AddonDownloads::ReleaseFor(int64_t steamId, uint64_t id)
{
    if (const auto found = _clients.find(steamId); found != _clients.end())
        RemoveHolder(found->second.Required, id);
}

bool AddonDownloads::Empty() const
{
    return _everyone.empty() && std::ranges::all_of(_clients, [](const auto& e) { return e.second.Required.empty(); });
}

std::vector<uint64_t> AddonDownloads::Required() const
{
    std::vector<uint64_t> ids;
    ids.reserve(_everyone.size());
    for (const Requirement& requirement : _everyone)
        ids.push_back(requirement.Id);
    return ids;
}

std::vector<uint64_t> AddonDownloads::MissingFor(int64_t steamId) const
{
    std::vector<uint64_t> missing = Required();

    if (const auto found = _clients.find(steamId); found != _clients.end())
    {
        for (const Requirement& own : found->second.Required)
            if (!std::ranges::contains(missing, own.Id))
                missing.push_back(own.Id);

        for (uint64_t downloaded : found->second.Downloaded)
            std::erase(missing, downloaded);
    }

    return missing;
}

bool AddonDownloads::HasMissing(int64_t steamId) const
{
    const auto found = _clients.find(steamId);
    if (found == _clients.end())
        return !_everyone.empty();

    const auto& downloaded = found->second.Downloaded;
    const auto missing = [&downloaded](const Requirement& requirement) {
        return !std::ranges::contains(downloaded, requirement.Id);
    };
    return std::ranges::any_of(_everyone, missing) || std::ranges::any_of(found->second.Required, missing);
}

AddonDecision AddonDownloads::NextToSend(int64_t steamId, double now, int maxAttempts)
{
    const std::vector<uint64_t> missing = MissingFor(steamId);
    if (missing.empty())
        return {};

    Client& client = _clients[steamId];
    const uint64_t next = missing.front();

    // The same addon coming round again means the last offer was not taken.
    client.Attempts = (client.Sending == next) ? client.Attempts + 1 : 1;
    if (client.Attempts > maxAttempts)
        return {.Action = AddonAction::DropClient, .Id = next};

    client.Sending = next;
    client.SentAt = now;
    return {.Action = AddonAction::Send, .Id = next, .Remaining = missing.size() - 1};
}

AddonDecision AddonDownloads::DecideJoinMessage(int64_t steamId, bool reconnect, std::string_view addons, double now,
                                                int maxAttempts)
{
    if (!reconnect)
        return NextToSend(steamId, now, maxAttempts);

    // The client handles only the first addon; the rest wait for a later reconnect.
    const std::vector<uint64_t> listed = ParseAddonList(addons);
    if (listed.empty())
        return {};

    MarkSending(steamId, listed.front(), now);
    if (listed.size() == 1)
        return {};
    return {.Action = AddonAction::KeepFirst, .Id = listed.front(), .Remaining = listed.size() - 1};
}

void AddonDownloads::MarkSending(int64_t steamId, uint64_t id, double now)
{
    if (id == 0)
        return;

    Client& client = _clients[steamId];
    client.Sending = id;
    client.SentAt = now;
    client.Attempts = 0;
}

void AddonDownloads::RecordReconnect(int64_t steamId, double now, double timeoutSec)
{
    const auto found = _clients.find(steamId);
    if (found == _clients.end() || found->second.Sending == 0)
        return;

    Client& client = found->second;
    if (now - client.SentAt <= timeoutSec)
    {
        if (!std::ranges::contains(client.Downloaded, client.Sending))
            client.Downloaded.push_back(client.Sending);
        client.Attempts = 0;
    }
    client.Sending = 0;
}

void AddonDownloads::ClearProgress()
{
    for (auto it = _clients.begin(); it != _clients.end();)
    {
        if (it->second.Required.empty())
        {
            it = _clients.erase(it);
            continue;
        }

        // Rebuilt rather than reset field by field, so a progress field added later is cleared too.
        it->second = Client{.Required = std::move(it->second.Required)};
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

        if (auto id = ParseUInt64(token); id && *id != 0)
            ids.push_back(*id);

        if (comma == std::string_view::npos)
            break;
        field.remove_prefix(comma + 1);
    }

    return ids;
}

}  // namespace VoltMod
