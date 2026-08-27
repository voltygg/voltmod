#include "Targeting.hpp"

#include <VoltMod/Core/SteamId.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <charconv>
#include <utility>

namespace VoltMod
{

// Engine team indices (mirrors TeamT/TeamCT/TeamSpectator without the SDK include).
static constexpr int TeamSpectator = 1;
static constexpr int TeamT = 2;
static constexpr int TeamCT = 3;

std::optional<int64_t> ParseInt64(std::string_view text)
{
    int64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<uint64_t> ParseUInt64(std::string_view text)
{
    uint64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

TargetQuery ParseTargetToken(std::string_view token)
{
    const std::string raw(token);
    const std::string lower = Strings::ToLower(raw);
    using Kind = TargetKind;

    if (lower == "@all" || lower == "@*")
        return {.Kind = Kind::All};
    if (lower == "@me")
        return {.Kind = Kind::Me};
    if (lower == "@!me")
        return {.Kind = Kind::NotMe};
    if (lower == "@t")
        return {.Kind = Kind::Team, .Team = TeamT};
    if (lower == "@ct")
        return {.Kind = Kind::Team, .Team = TeamCT};
    if (lower == "@spec")
        return {.Kind = Kind::Team, .Team = TeamSpectator};
    if (lower == "@dead")
        return {.Kind = Kind::Dead};
    if (lower == "@alive")
        return {.Kind = Kind::Alive};
    if (lower == "@bot" || lower == "@bots")
        return {.Kind = Kind::Bots};
    if (lower == "@human" || lower == "@humans")
        return {.Kind = Kind::Humans};
    if (lower == "@random")
        return {.Kind = Kind::Random};
    if (lower == "@randomt")
        return {.Kind = Kind::RandomTeam, .Team = TeamT};
    if (lower == "@randomct")
        return {.Kind = Kind::RandomTeam, .Team = TeamCT};

    if (raw.size() > 1 && raw[0] == '#')
    {
        // in_range before the cast: a value above INT_MAX would otherwise wrap to an
        // arbitrary slot and match whoever occupies it.
        if (auto slot = ParseInt64(std::string_view(raw).substr(1)); slot && std::in_range<int>(*slot) && *slot >= 0)
            return {.Kind = Kind::Slot, .Slot = static_cast<int>(*slot)};
    }

    if (raw.size() >= 15)
    {
        if (auto id = ParseInt64(raw); id && SteamId::IsValid(*id))
            return {.Kind = Kind::SteamId, .SteamId = *id};
    }
    if (Strings::StartsWith(raw, "[U:1:"))
    {
        if (auto id = SteamId::FromSteamId3(raw))
            return {.Kind = Kind::SteamId, .SteamId = *id};
    }
    if (Strings::StartsWith(raw, "STEAM_"))
    {
        if (auto id = SteamId::FromSteamId(raw))
            return {.Kind = Kind::SteamId, .SteamId = *id};
    }

    return {.Kind = Kind::Name, .Needle = lower};
}

std::expected<std::vector<int>, TargetFailure> FilterRoster(std::span<const PlayerView> roster,
                                                            const TargetQuery& query, const TargetRules& rules,
                                                            int callerSlot,
                                                            const std::function<std::size_t(std::size_t)>& randomIndex)
{
    using Kind = TargetKind;

    // 1. Collect candidates by query kind.
    std::vector<const PlayerView*> candidates;
    auto collect = [&](auto&& pred) {
        for (const auto& p : roster)
            if (pred(p))
                candidates.push_back(&p);
    };

    switch (query.Kind)
    {
    case Kind::All:
    case Kind::Random:
        collect([](const PlayerView&) { return true; });
        break;
    case Kind::Me:
        collect([&](const PlayerView& p) { return p.Slot == callerSlot; });
        break;
    case Kind::NotMe:
        collect([&](const PlayerView& p) { return p.Slot != callerSlot; });
        break;
    case Kind::Team:
    case Kind::RandomTeam:
        collect([&](const PlayerView& p) { return p.Team == query.Team; });
        break;
    case Kind::Dead:
        collect([](const PlayerView& p) { return !p.Alive; });
        break;
    case Kind::Alive:
        collect([](const PlayerView& p) { return p.Alive; });
        break;
    case Kind::Bots:
        collect([](const PlayerView& p) { return p.Bot; });
        break;
    case Kind::Humans:
        collect([](const PlayerView& p) { return !p.Bot; });
        break;
    case Kind::Slot:
        collect([&](const PlayerView& p) { return p.Slot == query.Slot; });
        break;
    case Kind::SteamId:
        collect([&](const PlayerView& p) { return p.SteamId == query.SteamId; });
        break;
    case Kind::Name:
    {
        // Tiered matching: an exact name wins outright, then prefixes, then substrings -
        // so "bob" targets Bob even when "bobby" is also online.
        auto tier = [&](auto&& pred) {
            candidates.clear();
            collect(pred);
            return !candidates.empty();
        };
        tier([&](const PlayerView& p) { return Strings::ToLower(p.Name) == query.Needle; }) ||
            tier([&](const PlayerView& p) { return Strings::StartsWith(Strings::ToLower(p.Name), query.Needle); }) ||
            tier([&](const PlayerView& p) { return Strings::ToLower(p.Name).find(query.Needle) != std::string::npos; });
        break;
    }
    }

    if (candidates.empty())
        return std::unexpected(TargetFailure{TargetError::NoMatch});

    // 2. Apply command rules, tracking which filter emptied the set.
    auto drop = [&](auto&& pred) { std::erase_if(candidates, pred); };

    if (!rules.AllowBots)
    {
        drop([](const PlayerView* p) { return p->Bot; });
        if (candidates.empty())
            return std::unexpected(TargetFailure{TargetError::BotNotAllowed});
    }
    if (!rules.AllowDead)
    {
        drop([](const PlayerView* p) { return !p->Alive; });
        if (candidates.empty())
            return std::unexpected(TargetFailure{TargetError::DeadNotAllowed});
    }

    // 3. Immunity policy: skip blocked players; error only when it blocked everyone.
    drop([](const PlayerView* p) { return !p->Targetable; });
    if (candidates.empty())
        return std::unexpected(TargetFailure{TargetError::Immune});

    // 4. Random kinds resolve to exactly one entry.
    if (query.Kind == Kind::Random || query.Kind == Kind::RandomTeam)
    {
        std::size_t pick = randomIndex ? randomIndex(candidates.size()) % candidates.size() : 0;
        candidates = {candidates[pick]};
    }

    // 5. Single-target commands reject multi results: a name fragment is ambiguous (narrowable),
    //    a deliberate multi-selector is simply not allowed here.
    if (candidates.size() > 1 && !rules.AllowMultiple)
    {
        auto error = (query.Kind == Kind::Name) ? TargetError::Ambiguous : TargetError::MultiNotAllowed;
        return std::unexpected(TargetFailure{error, static_cast<int>(candidates.size())});
    }

    std::vector<int> slots;
    slots.reserve(candidates.size());
    for (const auto* p : candidates)
        slots.push_back(p->Slot);
    return slots;
}

}  // namespace VoltMod
