#pragma once

#include <CS2Kit/Commands/CommandSpec.hpp>
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

    std::unordered_map<std::string, CommandSpec> _commands;
    /** Lowercased alias -> the lowercased command name that owns it. */
    std::unordered_map<std::string, std::string> _aliases;
    std::vector<std::string> _prefixes{"!", "."};
    /** Command names already reported for a missing HasPermission policy, so the error is
     *  logged once rather than on every attempt. */
    std::unordered_set<std::string> _missingPolicyWarned;
};

}  // namespace CS2Kit::Commands
