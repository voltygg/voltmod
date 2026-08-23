#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Core/StringUtils.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Players/TargetResolver.hpp>
#include <CS2Kit/Runtime.hpp>
#include <charconv>
#include <limits>

namespace CS2Kit::Commands
{

using namespace CS2Kit::Core;

namespace
{

std::optional<int64_t> ParseInt64(const std::string& text)
{
    int64_t value{};
    auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::string TargetErrorMessage(const Players::TargetFailure& failure, const std::string& token, int slot)
{
    auto& tr = CS2Kit::Detail::Rt().Translations;
    switch (failure.Error)
    {
    case Players::TargetError::Immune:
        return tr.Get("target.immune", slot, {{"token", token}});
    case Players::TargetError::Ambiguous:
    case Players::TargetError::MultiNotAllowed:
        return tr.Get("target.ambiguous", slot, {{"token", token}, {"count", std::to_string(failure.Count)}});
    case Players::TargetError::DeadNotAllowed:
        return tr.Get("target.dead", slot, {{"token", token}});
    case Players::TargetError::BotNotAllowed:
        return tr.Get("target.bot", slot, {{"token", token}});
    case Players::TargetError::NoMatch:
    default:
        return tr.Get("target.noMatch", slot, {{"token", token}});
    }
}

}  // namespace

void CommandManager::Register(CommandSpec spec)
{
    const std::string name = StringUtils::ToLower(spec.Name);

    if (_commands.contains(name) || _aliases.contains(name))
    {
        Log::Error("Command '{}' is already registered - ignoring the second registration.", spec.Name);
        return;
    }

    // Index aliases up front. Lookup used to fall back to a linear scan over an unordered_map,
    // so two commands sharing an alias resolved to whichever the bucket order happened to reach
    // first - stable within a run, arbitrary between builds.
    std::vector<std::string> claimed;
    claimed.reserve(spec.Aliases.size());
    for (const auto& alias : spec.Aliases)
    {
        std::string key = StringUtils::ToLower(alias);
        if (key.empty() || key == name)
            continue;

        if (_commands.contains(key))
        {
            Log::Error("Command '{}' claims alias '{}', which is already a command name - skipping the alias.",
                       spec.Name, alias);
            continue;
        }
        if (auto it = _aliases.find(key); it != _aliases.end())
        {
            Log::Error("Command '{}' claims alias '{}', already taken by '{}' - skipping the alias.", spec.Name,
                       alias, it->second);
            continue;
        }
        claimed.push_back(std::move(key));
    }

    for (auto& key : claimed)
        _aliases.emplace(std::move(key), name);

    _commands[name] = std::move(spec);
}

void CommandManager::Unregister(const std::string& name)
{
    const std::string key = StringUtils::ToLower(name);
    _commands.erase(key);
    std::erase_if(_aliases, [&](const auto& entry) { return entry.second == key; });
}

bool CommandManager::HandleChatMessage(Players::Player* caller, std::string_view message)
{
    if (!caller || message.empty())
        return false;

    bool hasPrefix = false;
    size_t prefixLen = 0;
    for (const auto& prefix : _prefixes)
    {
        if (message.size() >= prefix.size() && message.compare(0, prefix.size(), prefix) == 0)
        {
            hasPrefix = true;
            prefixLen = prefix.size();
            break;
        }
    }

    if (!hasPrefix)
        return false;

    auto parts = ParseArguments(std::string(message.substr(prefixLen)));
    if (parts.empty())
        return false;

    const std::string& cmdName = parts[0];
    std::vector<std::string> args(parts.begin() + 1, parts.end());

    const CommandSpec* cmd = GetCommand(cmdName);
    if (!cmd)
        return false;

    auto& policy = CS2Kit::Detail::Rt().Policy;
    auto reply = [&](const std::string& msg) {
        if (msg.empty())
            return;
        if (policy.Reply)
            policy.Reply(caller->GetSlot(), msg);
        else
            CS2Kit::Detail::Rt().Messages.Reply(caller->GetSlot(), msg);
    };

    if (!cmd->Permission.empty())
    {
        // No policy means no way to tell an admin from anyone else, so the only safe answer is
        // no. Warn once per command: a plugin that declares permissions and forgets to install
        // a policy is misconfigured, and the old behaviour let everyone through silently.
        if (!policy.HasPermission)
        {
            if (_missingPolicyWarned.insert(cmd->Name).second)
                Log::Error("Command '{}' declares permission '{}' but no HasPermission policy is "
                           "installed - denying. Set Runtime::Policy.HasPermission in OnLoad.",
                           cmd->Name, cmd->Permission);
            reply(CS2Kit::Detail::Rt().Translations.Get("cmd.noPermission", caller->GetSlot()));
            return true;
        }

        if (!policy.HasPermission(caller->GetSteamID(), cmd->Permission))
        {
            reply(CS2Kit::Detail::Rt().Translations.Get("cmd.noPermission", caller->GetSlot()));
            return true;
        }
    }

    CommandContext ctx;
    ctx.Caller = caller;
    ctx.RawArgs = args;

    // Extras used to be dropped in silence, so a typo'd selector looked like it worked.
    if (TooManyArguments(*cmd, args))
    {
        reply(CS2Kit::Detail::Rt().Translations.Get(
            "cmd.tooManyArgs", caller->GetSlot(),
            {{"usage", cmd->Usage.empty() ? DeriveUsage(*cmd) : cmd->Usage}}));
        return true;
    }

    std::string error;
    if (!ResolveArgs(*cmd, args, ctx, error))
    {
        reply(error.empty() ? "Usage: " + (cmd->Usage.empty() ? DeriveUsage(*cmd) : cmd->Usage) : error);
        return true;
    }

    if (cmd->Handler)
    {
        auto result = cmd->Handler(ctx);
        reply(result.Message);
    }

    return true;
}

bool CommandManager::TooManyArguments(const CommandSpec& cmd, const std::vector<std::string>& args) const
{
    // A ReasonTail swallows the remainder, so anything with one can never have extras.
    for (const auto& spec : cmd.Args)
        if (spec.Kind == ArgKind::ReasonTail)
            return false;

    return args.size() > cmd.Args.size();
}

bool CommandManager::ResolveArgs(const CommandSpec& cmd, const std::vector<std::string>& args, CommandContext& ctx,
                                 std::string& outError) const
{
    auto& tr = CS2Kit::Detail::Rt().Translations;
    const int slot = ctx.CallerSlot();
    std::size_t i = 0;

    auto fail = [&](const ArgSpec& spec, const char* defaultKey, Core::Tokens tokens = {}) {
        outError = tr.Get(spec.ErrorKey.empty() ? defaultKey : spec.ErrorKey.c_str(), slot, tokens);
        return false;
    };

    for (const auto& spec : cmd.Args)
    {
        const bool haveToken = i < args.size();
        if (!haveToken)
        {
            if (spec.Kind == ArgKind::ReasonTail)
            {
                // Fallback reasons use the server language - they land in the DB and broadcasts.
                ctx.Reason = spec.FallbackKey.empty() ? "" : tr.Get(spec.FallbackKey);
                continue;
            }
            if (!spec.Required)
                continue;
            return false;  // empty outError => generic usage reply
        }

        const std::string& token = args[i];
        switch (spec.Kind)
        {
        case ArgKind::Target:
        {
            auto resolved = Players::ResolveTargets(token, ctx.Caller, spec.Targeting);
            if (!resolved)
            {
                outError = TargetErrorMessage(resolved.error(), token, slot);
                return false;
            }
            ctx.TargetPlayer = resolved->front();
            if (spec.Targeting.AllowMultiple)
                ctx.TargetList = std::move(*resolved);
            ++i;
            break;
        }
        case ArgKind::TargetOrSteamId:
        {
            // A bare numeric token addresses an offline player by SteamID; anything else must
            // resolve to an online player (whose SteamID is then captured too).
            if (auto id = ParseInt64(token); id && StringUtils::IsNumeric(token))
            {
                ctx.SteamId = *id;
            }
            else
            {
                auto resolved = Players::ResolveTargets(token, ctx.Caller, {});
                if (!resolved)
                {
                    outError = TargetErrorMessage(resolved.error(), token, slot);
                    return false;
                }
                ctx.TargetPlayer = resolved->front();
                ctx.SteamId = ctx.Target->GetSteamID();
            }
            ++i;
            break;
        }
        case ArgKind::Duration:
        {
            int seconds = ParseDuration(token);
            if (seconds < 0)
                return fail(spec, "cmd.badDuration");
            // A bare number keeps the legacy command meaning (minutes); ParseDuration read it as seconds.
            ctx.DurationSec = (spec.BareNumbersAreMinutes && StringUtils::IsNumeric(token))
                                  ? static_cast<int64_t>(seconds) * 60
                                  : seconds;
            ++i;
            break;
        }
        case ArgKind::SteamId64:
        {
            auto id = ParseInt64(token);
            if (!id || !StringUtils::IsNumeric(token))
                return fail(spec, "cmd.badSteamId", {{"token", token}});
            ctx.SteamId = *id;
            ++i;
            break;
        }
        case ArgKind::Int:
        {
            auto value = ParseInt64(token);
            if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max())
            {
                // Every other kind reports through fail(); Int used to return a bare false, so a
                // bad number printed the usage line and never its own error key.
                return fail(spec, "cmd.badNumber", {{"token", token}});
            }
            ctx.IntValue = static_cast<int>(*value);
            ++i;
            break;
        }
        case ArgKind::Word:
            ctx.Word = token;
            ++i;
            break;
        case ArgKind::ReasonTail:
        {
            std::vector<std::string> rest(args.begin() + static_cast<std::ptrdiff_t>(i), args.end());
            ctx.Reason = StringUtils::Join(rest, " ");
            i = args.size();
            break;
        }
        }
    }

    return true;
}

const CommandSpec* CommandManager::GetCommand(const std::string& name) const
{
    const std::string key = StringUtils::ToLower(name);

    if (auto it = _commands.find(key); it != _commands.end())
        return &it->second;

    if (auto alias = _aliases.find(key); alias != _aliases.end())
    {
        if (auto it = _commands.find(alias->second); it != _commands.end())
            return &it->second;
    }

    return nullptr;
}

std::vector<const CommandSpec*> CommandManager::GetAllCommands() const
{
    std::vector<const CommandSpec*> commands;
    commands.reserve(_commands.size());

    for (const auto& [name, cmd] : _commands)
    {
        commands.push_back(&cmd);
    }

    return commands;
}

std::vector<std::string> CommandManager::ParseArguments(const std::string& text) const
{
    // Drop empty tokens so leading/trailing/repeated spaces (e.g. "ban  Bob") don't yield blank args.
    std::vector<std::string> parts;
    for (auto& token : StringUtils::Split(text, ' '))
        if (!token.empty())
            parts.push_back(std::move(token));
    return parts;
}

}  // namespace CS2Kit::Commands
