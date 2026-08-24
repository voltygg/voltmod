#pragma once

#include <CS2Kit/Commands/CommandSpec.hpp>
#include <CS2Kit/Sdk/ServerCommand.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CS2Kit::Commands
{

/**
 * @brief Dispatches chat commands (prefixed with ! or .) to registered CommandSpecs.
 *
 * The pipeline per message: prefix match -> spec lookup -> permission check
 * (`runtime.Policy.HasPermission`, which denies when unset) -> typed argument resolution (targets, durations,
 * SteamIDs - see @ref ArgKind) -> handler -> result message via `runtime.Policy.Reply`.
 * Handlers only run with fully-resolved, validated arguments.
 *
 * A spec that names Surface::Console additionally gets a tier1 ConCommand of the same name,
 * running the same resolution and handler with no caller and printing its reply to the
 * console. Registration owns that ConCommand, so Unregister and destruction remove it.
 */
class CommandManager
{
public:
    CommandManager() = default;

    void Register(CommandSpec spec);

    void Unregister(const std::string& name);
    bool HandleChatMessage(Players::Player* caller, std::string_view message);
    const CommandSpec* GetCommand(const std::string& name) const;
    std::vector<const CommandSpec*> GetAllCommands() const;
    size_t Count() const { return _commands.size(); }

    void SetPrefixes(const std::vector<std::string>& prefixes) { _prefixes = prefixes; }

private:
    std::vector<std::string> ParseArguments(const std::string& text) const;

    /** Resolve @p args against the spec into @p ctx. On failure @p outError holds the localized
     *  message ("" = generic usage line). */
    bool ResolveArgs(const CommandSpec& cmd, const std::vector<std::string>& args, CommandContext& ctx,
                     std::string& outError) const;

    /** True when more tokens were given than the spec can consume. */
    bool TooManyArguments(const CommandSpec& cmd, const std::vector<std::string>& args) const;

    /** Resolve and run @p cmd for @p caller (null = console), sending every reply to @p reply. */
    void Dispatch(const CommandSpec& cmd, Players::Player* caller, std::vector<std::string> args,
                  const std::function<void(const std::string&)>& reply);

    /** Bind a Surface::Console spec to a ConCommand of the same name. @p name must be the
     *  lowercased key @p spec is stored under. */
    void RegisterConsoleCommand(const std::string& name, const CommandSpec& spec);

    std::unordered_map<std::string, CommandSpec> _commands;
    /** Lowercased alias -> the lowercased command name that owns it. */
    std::unordered_map<std::string, std::string> _aliases;
    std::vector<std::string> _prefixes{"!", "."};
    /** Command names already reported for a missing HasPermission policy, so the error is
     *  logged once rather than on every attempt. */
    std::unordered_set<std::string> _missingPolicyWarned;
    /** Lowercased command name -> its ConCommand registration, for Surface::Console specs. */
    std::unordered_map<std::string, std::unique_ptr<Sdk::ServerCommand>> _consoleCommands;
};

}  // namespace CS2Kit::Commands
