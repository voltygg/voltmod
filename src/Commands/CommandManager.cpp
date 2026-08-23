#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Players/TargetResolver.hpp>
#include <CS2Kit/Utils/StringUtils.hpp>
#include <charconv>

namespace CS2Kit::Commands
{

using namespace CS2Kit::Utils;

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
    auto& tr = Core::Engine().Translations;
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
    _commands[StringUtils::ToLower(spec.Name)] = std::move(spec);
}

void CommandManager::RegisterAll(std::span<const CommandSpec> specs)
{
    for (const auto& spec : specs)
        Register(spec);
}

void CommandManager::Unregister(const std::string& name)
{
    _commands.erase(StringUtils::ToLower(name));
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

    auto& policy = Core::Engine().Policy;
    auto reply = [&](const std::string& msg) {
        if (msg.empty())
            return;
        if (policy.Reply)
            policy.Reply(caller->GetSlot(), msg);
        else
            Core::Engine().Sdk.Messages.Reply(caller->GetSlot(), msg);
    };

    if (!cmd->Permission.empty())
    {
        if (policy.HasPermission && !policy.HasPermission(caller->GetSteamID(), cmd->Permission))
        {
            reply("You do not have permission to use this command.");
            return true;
        }
    }

    CommandContext ctx;
    ctx.Caller = caller;
    ctx.RawArgs = args;

    std::string error;
    if (!ResolveArgs(*cmd, args, ctx, error))
    {
        reply(error.empty() ? "Usage: " + (cmd->Usage.empty() ? cmd->Name : cmd->Usage) : error);
        return true;
    }

    if (cmd->Handler)
    {
        auto result = cmd->Handler(ctx);
        reply(result.Message);
    }

    return true;
}

bool CommandManager::ResolveArgs(const CommandSpec& cmd, const std::vector<std::string>& args, CommandContext& ctx,
                                 std::string& outError) const
{
    auto& tr = Core::Engine().Translations;
    const int slot = ctx.CallerSlot();
    std::size_t i = 0;

    auto fail = [&](const ArgSpec& spec, const char* defaultKey, Utils::Tokens tokens = {}) {
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
            ctx.Target = resolved->front();
            if (spec.Targeting.AllowMultiple)
                ctx.Targets = std::move(*resolved);
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
                ctx.Target = resolved->front();
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
            if (!value)
                return false;
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
    auto it = _commands.find(StringUtils::ToLower(name));
    if (it != _commands.end())
        return &it->second;

    for (const auto& [key, cmd] : _commands)
    {
        if (cmd.Matches(name))
            return &cmd;
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
