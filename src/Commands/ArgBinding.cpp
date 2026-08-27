#include "Commands/ArgBinding.hpp"

#include <VoltMod/Core/Strings.hpp>
#include <utility>

namespace VoltMod
{

/** The reply key and tokens for a target that did not resolve. */
static ArgError TargetKey(const TargetFailure& failure, const std::string& token)
{
    switch (failure.Error)
    {
    case TargetError::Immune:
        return {.Key = "target.immune", .Vars = {{"token", token}}};
    case TargetError::Ambiguous:
    case TargetError::MultiNotAllowed:
        return {.Key = "target.ambiguous", .Vars = {{"token", token}, {"count", std::to_string(failure.Count)}}};
    case TargetError::DeadNotAllowed:
        return {.Key = "target.dead", .Vars = {{"token", token}}};
    case TargetError::BotNotAllowed:
        return {.Key = "target.bot", .Vars = {{"token", token}}};
    case TargetError::NoMatch:
    default:
        return {.Key = "target.noMatch", .Vars = {{"token", token}}};
    }
}

/** One token to the single player it names, or the key explaining why it named none. */
static std::expected<Player*, ArgError> BindTarget(ArgBinder& binder, const std::string& token, Player* caller)
{
    auto resolved = binder.Resolve(token, caller, TargetRules{});
    if (!resolved)
        return std::unexpected(TargetKey(resolved.error(), token));
    return resolved->front();
}

/**
 * One token to a player if anybody online answers to it, otherwise to a bare SteamID64.
 *
 * The fallback is only for NoMatch: falling back on Immune would turn a refused target into an
 * offline one and act on it anyway, and on Ambiguous it would pick nobody's id.
 */
static std::expected<Args::PlayerOrSteamId, ArgError> BindPlayerOrSteamId(ArgBinder& binder, const std::string& token,
                                                                          Player* caller)
{
    auto resolved = binder.Resolve(token, caller, TargetRules{});
    if (resolved)
    {
        Player* player = resolved->front();
        return Args::PlayerOrSteamId{.Online = player, .SteamId = player->SteamId()};
    }

    if (resolved.error().Error == TargetError::NoMatch && Strings::IsNumeric(token))
        if (auto id = ParseInt64(token))
            return Args::PlayerOrSteamId{.Online = nullptr, .SteamId = *id};

    return std::unexpected(TargetKey(resolved.error(), token));
}

static std::expected<Args::Duration, ArgError> BindDuration(const std::string& token)
{
    const int seconds = ParseDuration(token);
    if (seconds < 0)
        return std::unexpected(ArgError{.Key = "cmd.badDuration", .Vars = {{"token", token}}});

    // ParseDuration reads a bare number as seconds; a command duration reads it as minutes.
    const int64_t value = Strings::IsNumeric(token) ? static_cast<int64_t>(seconds) * 60 : seconds;
    return Args::Duration{.Value = std::chrono::seconds{value}};
}

static std::expected<Args::SteamId, ArgError> BindSteamId(const std::string& token)
{
    auto id = ParseInt64(token);
    if (!id || !Strings::IsNumeric(token))
        return std::unexpected(ArgError{.Key = "cmd.badSteamId", .Vars = {{"token", token}}});
    return Args::SteamId{.Value = *id};
}

static std::expected<Args::Int, ArgError> BindInt(const std::string& token)
{
    auto value = ParseInt64(token);
    if (!value || !std::in_range<int>(*value))
        return std::unexpected(ArgError{.Key = "cmd.badNumber", .Vars = {{"token", token}}});
    return Args::Int{.Value = static_cast<int>(*value)};
}

static std::expected<Args::U64, ArgError> BindU64(const std::string& token)
{
    auto value = ParseUInt64(token);
    if (!value)
        return std::unexpected(ArgError{.Key = "cmd.badNumber", .Vars = {{"token", token}}});
    return Args::U64{.Value = *value};
}

/** Store @p parsed, or return its error. Each binder above returns its own argument type. */
template <class T>
static std::optional<ArgError> Store(std::expected<T, ArgError> parsed, std::vector<BoundArg>& bound)
{
    if (!parsed)
        return std::move(parsed.error());
    bound.emplace_back(std::move(*parsed));
    return std::nullopt;
}

std::expected<std::vector<BoundArg>, ArgError> BindArgs(const CommandDefinition& def,
                                                        std::span<const std::string> tokens, Player* caller,
                                                        ArgBinder& binder)
{
    std::vector<BoundArg> bound;
    bound.reserve(def.Args.size());
    size_t i = 0;

    for (const ArgDesc& arg : def.Args)
    {
        if (i >= tokens.size())
        {
            // Arity was checked first, so only an optional argument can be missing here.
            bound.emplace_back(std::monostate{});
            continue;
        }

        const std::string& token = tokens[i];
        std::optional<ArgError> failed;

        switch (arg.Kind)
        {
        case ArgKind::Target:
            if (auto player = BindTarget(binder, token, caller))
                bound.emplace_back(Args::Target{.Value = *player});
            else
                failed = std::move(player.error());
            break;
        case ArgKind::PlayerOrSteamId:
            failed = Store(BindPlayerOrSteamId(binder, token, caller), bound);
            break;
        case ArgKind::Duration:
            failed = Store(BindDuration(token), bound);
            break;
        case ArgKind::SteamId:
            failed = Store(BindSteamId(token), bound);
            break;
        case ArgKind::Int:
            failed = Store(BindInt(token), bound);
            break;
        case ArgKind::U64:
            failed = Store(BindU64(token), bound);
            break;
        case ArgKind::Word:
            bound.emplace_back(Args::Word{.Value = token});
            break;
        case ArgKind::Rest:
        {
            // A Rest swallows the remainder, so it is the last argument by construction.
            std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
            bound.emplace_back(Args::Rest{.Value = Strings::Join(rest, " ")});
            i = tokens.size();
            continue;
        }
        }

        if (failed)
            return std::unexpected(std::move(*failed));
        ++i;
    }

    return bound;
}

}  // namespace VoltMod
