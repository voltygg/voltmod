#pragma once

#include <CS2Kit/Commands/CommandSpec.hpp>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CS2Kit::Commands
{

/**
 * @brief Dispatches chat commands (prefixed with ! or .) to registered CommandSpecs.
 *
 * The pipeline per message: prefix match -> spec lookup -> permission check
 * (`runtime.Policy.HasPermission`) -> typed argument resolution (targets, durations,
 * SteamIDs - see @ref ArgKind) -> handler -> result message via `runtime.Policy.Reply`.
 * Handlers only run with fully-resolved, validated arguments.
 */
class CommandManager
{
public:
    CommandManager() = default;

    void Register(CommandSpec spec);

    /** Bulk-ingest, typically `RegisterAll(Registry<CommandSpec>::Items())` in OnLoad. */
    void RegisterAll(std::span<const CommandSpec> specs);

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
    std::vector<std::string> _prefixes{"!", "."};
};

}  // namespace CS2Kit::Commands
