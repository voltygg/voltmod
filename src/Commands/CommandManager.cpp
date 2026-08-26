#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Players/TargetResolver.hpp>
#include <VoltMod/Runtime.hpp>
#include <algorithm>
#include <charconv>
#include <convar.h>
#include <limits>

namespace VoltMod::Commands
{

using namespace VoltMod::Core;

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

std::string TargetErrorMessage(Core::Translations& tr, const Players::TargetFailure& failure, const std::string& token,
                               int slot)
{
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
    const std::string name = Strings::ToLower(spec.Name);

    if (_commands.contains(name) || _aliases.contains(name))
    {
        Log::Error("Command '{}' is already registered - ignoring the second registration.", spec.Name);
        return;
    }

    // Index aliases once and reject collisions for deterministic lookup.
    for (const auto& alias : spec.Aliases)
    {
        std::string key = Strings::ToLower(alias);
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
            Log::Error("Command '{}' claims alias '{}', already taken by '{}' - skipping the alias.", spec.Name, alias,
                       it->second);
            continue;
        }
        _aliases.emplace(std::move(key), name);
    }

    const bool console = ReachableFrom(spec, Surface::Console);
    const CommandSpec& stored = (_commands[name] = std::move(spec));

    if (console)
        RegisterConsoleCommand(name, stored);
}

void CommandManager::RegisterConsoleCommand(const std::string& name, const CommandSpec& spec)
{
    // The console has no prefix to type, so the derived usage must not claim one.
    const std::string help = spec.Description.empty() ? DeriveUsage(spec, "") : spec.Description;
    _consoleCommands.emplace(
        name, std::make_unique<Engine::ServerCommand>(name.c_str(), help.c_str(), [this, name](const CCommand& args) {
            // Re-resolved per invocation: the spec can be unregistered while the ConCommand
            // lives. `name` is already the canonical key, so no lowering or alias hop is needed.
            auto it = _commands.find(name);
            if (it == _commands.end())
                return;
            const CommandSpec* cmd = &it->second;

            std::vector<std::string> tokens;
            tokens.reserve(static_cast<size_t>(args.ArgC()));
            for (int i = 1; i < args.ArgC(); ++i)
                tokens.emplace_back(args.Arg(i));

            // The console has no chat window to reply into, and no language of its own.
            Dispatch(*cmd, nullptr, std::move(tokens), [](const std::string& msg) { Log::Info("{}", msg); });
        }));
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

    // Surfaces is what makes a console-only command console-only. Without this the Chat bit was
    // never read, so a spec registered as Surface::Console - typically an operator command with
    // no Permission, because the console needs none - was also typeable in chat by anyone.
    const CommandSpec* cmd = GetCommand(cmdName);
    if (!cmd || !ReachableFrom(*cmd, Surface::Chat))
        return false;

    auto& policy = _runtime.Policy;
    const int slot = caller->GetSlot();
    Dispatch(*cmd, caller, std::move(args), [&](const std::string& msg) {
        if (policy.Reply)
            policy.Reply(slot, msg);
        else
            _runtime.Messages.Reply(slot, msg);
    });

    return true;
}

void CommandManager::Dispatch(const CommandSpec& cmd, Players::Player* caller, std::vector<std::string> args,
                              const std::function<void(const std::string&)>& reply)
{
    // Console has no player and so no language of its own; slot -1 resolves the server language.
    const int slot = caller ? caller->GetSlot() : -1;
    auto& tr = _runtime.Translations;
    const auto say = [&](const std::string& msg) {
        if (!msg.empty())
            reply(msg);
    };
    // The prefix belongs to the surface being replied to, not to the spec: the console types none.
    const auto usage = [&] {
        if (!cmd.Usage.empty())
            return cmd.Usage;
        const std::string_view prefix = caller && !_prefixes.empty() ? _prefixes.front() : std::string_view{};
        return DeriveUsage(cmd, prefix);
    };

    // Permissions say which players may do this. The console is the server itself: no SteamID
    // to check, and nothing above it to deny it.
    if (caller && !cmd.Permission.empty())
    {
        auto& policy = _runtime.Policy;

        // Without policy there is no trusted permission source. Deny and warn once
        // per command so the plugin misconfiguration is visible.
        if (!policy.HasPermission)
        {
            if (_missingPolicyWarned.insert(cmd.Name).second)
                Log::Error(
                    "Command '{}' declares permission '{}' but no HasPermission policy is "
                    "installed - denying. Set Runtime::Policy.HasPermission in OnLoad.",
                    cmd.Name, cmd.Permission);
            say(tr.Get("cmd.noPermission", slot));
            return;
        }

        if (!policy.HasPermission(caller->GetSteamID(), cmd.Permission))
        {
            say(tr.Get("cmd.noPermission", slot));
            return;
        }
    }

    CommandContext ctx;
    ctx.Tr = &tr;  // handler-facing Ok()/Fail() translate through the runtime's table
    ctx.Caller = caller;

    // Reject extra tokens so malformed commands cannot appear successful.
    if (TooManyArguments(cmd, args.size()))
    {
        say(tr.Get("cmd.tooManyArgs", slot, {{"usage", usage()}}));
        return;
    }

    std::string error;
    if (!ResolveArgs(cmd, args, ctx, error))
    {
        say(error.empty() ? "Usage: " + usage() : error);
        return;
    }

    if (cmd.Handler)
        say(cmd.Handler(ctx).Message);
}

bool CommandManager::ResolveArgs(const CommandSpec& cmd, const std::vector<std::string>& args, CommandContext& ctx,
                                 std::string& outError) const
{
    auto& tr = _runtime.Translations;
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
            // Target() always resolves against the default rules: single online target,
            // dead and bots allowed.
            const Players::TargetRules rules{};
            auto resolved = Players::ResolveTargets(_runtime, token, ctx.Caller, rules);
            if (!resolved)
            {
                outError = TargetErrorMessage(tr, resolved.error(), token, slot);
                return false;
            }
            ctx.TargetPlayer = resolved->front();
            if (rules.AllowMultiple)
                ctx.TargetList = std::move(*resolved);
            ++i;
            break;
        }
        case ArgKind::TargetOrSteamId:
        {
            // A bare numeric token addresses an offline player by SteamID; anything else must
            // resolve to an online player (whose SteamID is then captured too).
            if (auto id = ParseInt64(token); id && Strings::IsNumeric(token))
            {
                ctx.SteamId = *id;
            }
            else
            {
                auto resolved = Players::ResolveTargets(_runtime, token, ctx.Caller, {});
                if (!resolved)
                {
                    outError = TargetErrorMessage(tr, resolved.error(), token, slot);
                    return false;
                }
                ctx.TargetPlayer = resolved->front();
                ctx.SteamId = ctx.Target().GetSteamID();
            }
            ++i;
            break;
        }
        case ArgKind::Duration:
        {
            int seconds = ParseDuration(token);
            if (seconds < 0)
                return fail(spec, "cmd.badDuration");
            // ParseDuration treats bare numbers as seconds; Duration() reinterprets them as minutes.
            ctx.DurationSec = Strings::IsNumeric(token) ? static_cast<int64_t>(seconds) * 60 : seconds;
            ++i;
            break;
        }
        case ArgKind::SteamId64:
        {
            auto id = ParseInt64(token);
            if (!id || !Strings::IsNumeric(token))
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
                // Report the integer-specific error rather than generic usage.
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
            ctx.Reason = Strings::Join(rest, " ");
            i = args.size();
            break;
        }
        }
    }

    return true;
}

std::vector<std::string> CommandManager::CommandsMissingPolicy() const
{
    if (_runtime.Policy.HasPermission)
        return {};

    std::vector<std::string> names;
    for (const auto& [key, spec] : _commands)
        if (!spec.Permission.empty())
            names.push_back(spec.Name);
    std::sort(names.begin(), names.end());  // map order is arbitrary; the report should not be
    return names;
}

const CommandSpec* CommandManager::GetCommand(const std::string& name) const
{
    const std::string key = Strings::ToLower(name);

    if (auto it = _commands.find(key); it != _commands.end())
        return &it->second;

    if (auto alias = _aliases.find(key); alias != _aliases.end())
    {
        if (auto it = _commands.find(alias->second); it != _commands.end())
            return &it->second;
    }

    return nullptr;
}

std::vector<std::string> CommandManager::ParseArguments(const std::string& text) const
{
    // Drop empty tokens so leading/trailing/repeated spaces (e.g. "ban  Bob") don't yield blank args.
    std::vector<std::string> parts;
    for (auto& token : Strings::Split(text, ' '))
        if (!token.empty())
            parts.push_back(std::move(token));
    return parts;
}

}  // namespace VoltMod::Commands
