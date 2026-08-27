#include "CommandRouter.hpp"

#include "Commands/CommandSyntax.hpp"

#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <algorithm>
#include <cctype>
#include <utility>

namespace VoltMod
{

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

bool CommandRouter::Add(CommandDefinition def)
{
    const std::string name = Strings::ToLower(def.Name);

    if (name.empty())
    {
        Log::Error("A command was registered with no name - ignoring it.");
        return false;
    }
    if (_commands.contains(name) || _aliases.contains(name))
    {
        Log::Error("Command '{}' is already registered - ignoring the second registration.", def.Name);
        return false;
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

    _commands.emplace(name, std::move(def));
    return true;
}

void CommandRouter::Clear()
{
    _commands.clear();
    _aliases.clear();
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
    std::string prefix = origin == Origin::Chat ? std::string(CommandSyntax::ChatPrefix()) : std::string{};

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

    const Caller who{.Player = caller, .Slot = slot, .Tr = _translations, .Send = say};
    auto result = def.Invoke(who, *bound);
    if (result)
        reply(result->Text);
    else
        // Caller::Fail localized as it built the error; anything else carries only a key.
        reply(result.error().Detail.empty() ? _translations.Get(result.error().Key, slot) : result.error().Detail);
}

}  // namespace VoltMod
