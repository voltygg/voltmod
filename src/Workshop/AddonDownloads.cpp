#include "Workshop/AddonDownloads.hpp"

#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <string>
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

const AddonDownloads::Client* AddonDownloads::FindClient(int64_t steamId) const
{
    const auto found = _clients.find(steamId);
    return found != _clients.end() ? &found->second : nullptr;
}

std::vector<uint64_t> AddonDownloads::RequiredFor(const Client* client) const
{
    std::vector<uint64_t> ids = Required();

    if (client)
    {
        for (const Requirement& own : client->Required)
            if (!std::ranges::contains(ids, own.Id))
                ids.push_back(own.Id);
    }

    return ids;
}

std::vector<uint64_t> AddonDownloads::MissingFor(int64_t steamId) const
{
    const Client* client = FindClient(steamId);
    std::vector<uint64_t> missing = RequiredFor(client);
    if (client)
        std::erase_if(missing, [client](uint64_t id) { return std::ranges::contains(client->Downloaded, id); });
    return missing;
}

std::vector<uint64_t> AddonDownloads::ToMount(int64_t steamId) const
{
    const Client* client = FindClient(steamId);
    if (!client)
        return {};

    std::vector<uint64_t> ids = RequiredFor(client);
    std::erase_if(ids, [client](uint64_t id) {
        return id != client->Sending && !std::ranges::contains(client->Downloaded, id);
    });
    return ids;
}

bool AddonDownloads::HasMissing(int64_t steamId) const
{
    const Client* client = FindClient(steamId);
    if (!client)
        return !_everyone.empty();

    const auto missing = [client](const Requirement& requirement) {
        return !std::ranges::contains(client->Downloaded, requirement.Id);
    };
    return std::ranges::any_of(_everyone, missing) || std::ranges::any_of(client->Required, missing);
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
        return {.Action = AddonAction::Kick, .Id = next};

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
    return {.Action = AddonAction::TrimToFirst, .Id = listed.front(), .Remaining = listed.size() - 1};
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

/** The comma-separated entries of @p field, verbatim; none for an empty field. */
static std::vector<std::string_view> SplitAddonList(std::string_view field)
{
    std::vector<std::string_view> entries;

    while (!field.empty())
    {
        const size_t comma = field.find(',');
        entries.push_back(field.substr(0, comma));

        if (comma == std::string_view::npos)
            break;
        field.remove_prefix(comma + 1);
        if (field.empty())
            entries.emplace_back();
    }

    return entries;
}

std::vector<uint64_t> ParseAddonList(std::string_view field)
{
    std::vector<uint64_t> ids;
    for (std::string_view entry : SplitAddonList(field))
    {
        if (auto id = ParseUInt64(entry); id && *id != 0)
            ids.push_back(*id);
    }
    return ids;
}

std::vector<uint64_t> AppendToAddonList(std::string& field, const std::vector<uint64_t>& ids)
{
    const std::vector<uint64_t> named = ParseAddonList(field);
    std::vector<uint64_t> appended;

    for (uint64_t id : ids)
    {
        if (id == 0 || std::ranges::contains(named, id) || std::ranges::contains(appended, id))
            continue;

        if (!field.empty())
            field += ',';
        field += std::to_string(id);
        appended.push_back(id);
    }

    return appended;
}

void RemoveFromAddonList(std::string& field, const std::vector<uint64_t>& ids)
{
    std::vector<std::string_view> entries = SplitAddonList(field);
    for (uint64_t id : ids)
    {
        const auto at =
            std::ranges::find_if(entries, [id](std::string_view entry) { return ParseUInt64(entry) == id; });
        if (at != entries.end())
            entries.erase(at);
    }

    std::string kept;
    kept.reserve(field.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        if (i > 0)
            kept += ',';
        kept += entries[i];
    }
    field = std::move(kept);
}

}  // namespace VoltMod
