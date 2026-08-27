#include "CommandRouter.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <limits>

namespace VoltMod
{

/** The chat prefixes, in the order they are matched. The first is what a usage line shows. */
static const std::array<std::string_view, 2> kPrefixes{"!", "."};

static std::optional<int64_t> ParseInt64(const std::string& text)
{
    int64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

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

/** `cmd.usage.<argKind>`: the enumerator's own name, first letter lowered, so a new @ref ArgKind
 *  needs a translation key and no switch. */
static std::string UsagePlaceholderKey(ArgKind kind)
{
    std::string key = "cmd.usage.";
    const std::string_view name = Name(kind);
    if (!name.empty())
    {
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(name.front())));
        key += name.substr(1);
    }
    return key;
}

std::string_view CommandRouter::ChatPrefix()
{
    return kPrefixes.front();
}

std::vector<std::string> CommandRouter::Tokenize(std::string_view text)
{
    std::vector<std::string> tokens;
    std::string current;
    bool quoted = false;
    bool started = false;  // a `""` is an argument, even though the token is empty

    for (size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && text[i + 1] == '"')
        {
            current += '"';
            ++i;
            started = true;
            continue;
        }
        if (c == '"')
        {
            quoted = !quoted;
            started = true;
            continue;
        }
        if (c == ' ' && !quoted)
        {
            if (started || !current.empty())
                tokens.push_back(current);
            current.clear();
            started = false;
            continue;
        }
        current += c;
    }

    if (started || !current.empty())
        tokens.push_back(current);
    return tokens;
}

std::optional<std::string_view> CommandRouter::StripPrefix(std::string_view message)
{
    for (std::string_view prefix : kPrefixes)
        if (message.size() > prefix.size() && message.compare(0, prefix.size(), prefix) == 0)
            return message.substr(prefix.size());
    return std::nullopt;
}

uint64_t CommandRouter::Add(CommandDefinition def)
{
    const std::string name = Strings::ToLower(def.Name);

    if (name.empty())
    {
        Log::Error("A command was registered with no name - ignoring it.");
        return 0;
    }
    if (_commands.contains(name) || _aliases.contains(name))
    {
        Log::Error("Command '{}' is already registered - ignoring the second registration.", def.Name);
        return 0;
    }

    // Index aliases once and reject collisions, so lookup is deterministic.
    for (const std::string& alias : def.Aliases)
    {
        std::string key = Strings::ToLower(alias);
        if (key.empty() || key == name)
            continue;
        if (_commands.contains(key))
        {
            Log::Error("Command '{}' claims alias '{}', which is already a command name - skipping the alias.",
                       def.Name, alias);
            continue;
        }
        if (auto it = _aliases.find(key); it != _aliases.end())
        {
            Log::Error("Command '{}' claims alias '{}', already taken by '{}' - skipping the alias.", def.Name, alias,
                       it->second);
            continue;
        }
        _aliases.emplace(std::move(key), name);
    }

    const uint64_t id = _nextId++;
    _ids[name] = id;
    _commands.emplace(name, std::move(def));
    return id;
}

void CommandRouter::Remove(std::string_view name, uint64_t id)
{
    const std::string key = Strings::ToLower(std::string(name));
    auto owned = _ids.find(key);
    if (owned == _ids.end() || owned->second != id)
        return;  // already gone, or re-registered since; the newer registration is not ours

    _ids.erase(owned);
    _commands.erase(key);
    std::erase_if(_aliases, [&key](const auto& entry) { return entry.second == key; });
}

const CommandDefinition* CommandRouter::Find(std::string_view name) const
{
    const std::string key = Strings::ToLower(std::string(name));

    if (auto it = _commands.find(key); it != _commands.end())
        return &it->second;

    if (auto alias = _aliases.find(key); alias != _aliases.end())
        if (auto it = _commands.find(alias->second); it != _commands.end())
            return &it->second;

    return nullptr;
}

std::vector<std::string> CommandRouter::NamesWithPermission() const
{
    std::vector<std::string> names;
    for (const auto& [key, def] : _commands)
        if (!def.PermissionName.empty())
            names.push_back(def.Name);
    std::sort(names.begin(), names.end());
    return names;
}

size_t CommandRouter::RequiredArgs(const CommandDefinition& def)
{
    return static_cast<size_t>(
        std::count_if(def.Args.begin(), def.Args.end(), [](const ArgDesc& arg) { return !arg.Optional; }));
}

bool CommandRouter::HasRest(const CommandDefinition& def)
{
    return std::any_of(def.Args.begin(), def.Args.end(), [](const ArgDesc& arg) { return arg.Kind == ArgKind::Rest; });
}

Tokens CommandRouter::UsageTokens(const CommandDefinition& def, int slot, Origin origin) const
{
    // The prefix belongs to the surface being answered, not to the command: the console types none.
    std::string prefix = origin == Origin::Chat ? std::string(ChatPrefix()) : std::string{};

    std::string args;
    for (const ArgDesc& arg : def.Args)
    {
        if (!args.empty())
            args += ' ';
        const std::string placeholder = _translations.Get(UsagePlaceholderKey(arg.Kind), slot);
        args += arg.Optional ? '[' : '<';
        args += placeholder;
        args += arg.Optional ? ']' : '>';
    }

    std::string line = prefix + def.Name;
    if (!args.empty())
        line += ' ' + args;

    return {{"prefix", prefix}, {"command", def.Name}, {"args", args}, {"usage", line}};
}

std::string CommandRouter::Usage(const CommandDefinition& def, int slot, Origin origin) const
{
    if (!def.UsageKey.empty())
        return _translations.Get(def.UsageKey, slot);
    return _translations.Get("cmd.usage", slot, UsageTokens(def, slot, origin));
}

std::expected<std::vector<Player*>, ArgError> CommandRouter::ResolveArg(ArgBinder& binder, const std::string& token,
                                                                        Player* caller, const TargetRules& rules) const
{
    auto resolved = binder.Resolve(token, caller, rules);
    if (!resolved)
        return std::unexpected(TargetKey(resolved.error(), token));
    return std::move(*resolved);
}

std::expected<std::vector<BoundArg>, ArgError> CommandRouter::BindArgs(const CommandDefinition& def,
                                                                       std::span<const std::string> tokens,
                                                                       Player* caller, ArgBinder& binder) const
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
        switch (arg.Kind)
        {
        case ArgKind::Target:
        {
            auto resolved = ResolveArg(binder, token, caller, TargetRules{});
            if (!resolved)
                return std::unexpected(resolved.error());
            bound.emplace_back(Args::Target{.Value = resolved->front()});
            ++i;
            break;
        }
        case ArgKind::PlayerOrSteamId:
        {
            auto resolved = binder.Resolve(token, caller, TargetRules{});
            if (resolved)
            {
                Player* player = resolved->front();
                bound.emplace_back(Args::PlayerOrSteamId{.Online = player, .SteamId = player->SteamId()});
            }
            else if (auto id = ParseInt64(token); id && Strings::IsNumeric(token))
            {
                // Nobody online answers to it, but a bare SteamID64 still names somebody.
                bound.emplace_back(Args::PlayerOrSteamId{.Online = nullptr, .SteamId = *id});
            }
            else
            {
                return std::unexpected(TargetKey(resolved.error(), token));
            }
            ++i;
            break;
        }
        case ArgKind::Duration:
        {
            const int seconds = ParseDuration(token);
            if (seconds < 0)
                return std::unexpected(ArgError{.Key = "cmd.badDuration", .Vars = {{"token", token}}});
            // ParseDuration reads a bare number as seconds; a command duration reads it as minutes.
            const int64_t value = Strings::IsNumeric(token) ? static_cast<int64_t>(seconds) * 60 : seconds;
            bound.emplace_back(Args::Duration{.Value = std::chrono::seconds{value}});
            ++i;
            break;
        }
        case ArgKind::SteamId:
        {
            auto id = ParseInt64(token);
            if (!id || !Strings::IsNumeric(token))
                return std::unexpected(ArgError{.Key = "cmd.badSteamId", .Vars = {{"token", token}}});
            bound.emplace_back(Args::SteamId{.Value = *id});
            ++i;
            break;
        }
        case ArgKind::Int:
        {
            auto value = ParseInt64(token);
            if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
                return std::unexpected(ArgError{.Key = "cmd.badNumber", .Vars = {{"token", token}}});
            bound.emplace_back(Args::Int{.Value = static_cast<int>(*value)});
            ++i;
            break;
        }
        case ArgKind::Word:
            bound.emplace_back(Args::Word{.Value = token});
            ++i;
            break;
        case ArgKind::Rest:
        {
            std::vector<std::string> rest(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.end());
            bound.emplace_back(Args::Rest{.Value = Strings::Join(rest, " ")});
            i = tokens.size();
            break;
        }
        }
    }

    return bound;
}

void CommandRouter::Dispatch(const CommandDefinition& def, Player* caller, std::span<const std::string> tokens,
                             Origin origin, ArgBinder& binder, const std::function<void(const std::string&)>& say) const
{
    // The console has no player and so no language of its own; slot -1 is the server language.
    const int slot = caller ? caller->Slot() : -1;
    const auto reply = [&say](const std::string& line) {
        if (!line.empty())
            say(line);
    };

    // Permissions say which players may do this, and the one gate answers it. The console is the
    // server itself: no SteamID to check, and nothing above it to deny it.
    if (caller && !def.PermissionName.empty())
    {
        auto authorized = _policy.Authorize(caller->Ref(), std::nullopt, def.PermissionName);
        if (!authorized)
        {
            reply(_translations.Get(authorized.error().Key, slot));
            return;
        }
    }

    // Refuse extra tokens rather than dropping them, so a malformed command cannot look successful.
    if (!HasRest(def) && tokens.size() > def.Args.size())
    {
        reply(_translations.Get("cmd.tooManyArgs", slot, UsageTokens(def, slot, origin)));
        return;
    }

    if (tokens.size() < RequiredArgs(def))
    {
        reply(Usage(def, slot, origin));
        return;
    }

    auto bound = BindArgs(def, tokens, caller, binder);
    if (!bound)
    {
        reply(_translations.Get(bound.error().Key, slot, bound.error().Vars));
        return;
    }

    if (!def.Invoke)
        return;

    const Caller who{.P = caller, .Slot = slot, .Tr = _translations, .Send = say};
    auto result = def.Invoke(who, *bound);
    if (result)
        reply(result->Text);
    else
        // Caller::Fail localized as it built the error; anything else carries only a key.
        reply(result.error().Detail.empty() ? _translations.Get(result.error().Key, slot) : result.error().Detail);
}

}  // namespace VoltMod
