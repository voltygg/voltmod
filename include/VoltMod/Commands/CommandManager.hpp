#pragma once

#include <VoltMod/Commands/CommandSpec.hpp>
#include <VoltMod/Engine/ServerCommand.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace VoltMod
{
class Runtime;
}

namespace VoltMod::Commands
{

/**
 * @brief Dispatches chat commands (prefixed with ! or .) to registered CommandSpecs.
 *
 * The pipeline per message: prefix match -> spec lookup -> permission check
 * (`runtime.Policy.HasPermission`, which denies when unset) -> typed argument resolution (targets, durations,
 * SteamIDs - see @ref ArgKind) -> handler -> result message via `runtime.Policy.Reply`.
 * Handlers only run with fully-resolved, validated arguments.
 *
 * `Surfaces` gates both ends: a spec without Surface::Chat is not reachable from chat, and one
 * that names Surface::Console additionally gets a tier1 ConCommand of the same name,
 * running the same resolution and handler with no caller and printing its reply to the
 * console. Registration owns that ConCommand, so destruction removes it.
 */
class CommandManager
{
public:
    /** @p runtime supplies the policy, the translations, the roster and the reply fallback.
     *  It must outlive the manager, which the runtime's own declaration order guarantees. */
    explicit CommandManager(Runtime& runtime) : _runtime(runtime) {}

    void Register(CommandSpec spec);

    bool HandleChatMessage(Players::Player* caller, std::string_view message);
    size_t Count() const { return _commands.size(); }

    /** Names of registered specs that declare a Permission while no HasPermission policy is
     *  installed. Every one of them will be denied; MetamodPlugin reports this after OnLoad so
     *  the misconfiguration shows up in the load summary instead of the first time a player
     *  tries the command. */
    std::vector<std::string> CommandsMissingPolicy() const;

private:
    const CommandSpec* GetCommand(const std::string& name) const;
    std::vector<std::string> ParseArguments(const std::string& text) const;

    /** Resolve @p args against the spec into @p ctx. On failure @p outError holds the localized
     *  message ("" = generic usage line). */
    bool ResolveArgs(const CommandSpec& cmd, const std::vector<std::string>& args, CommandContext& ctx,
                     std::string& outError) const;

    /** Resolve and run @p cmd for @p caller (null = console), sending every reply to @p reply. */
    void Dispatch(const CommandSpec& cmd, Players::Player* caller, std::vector<std::string> args,
                  const std::function<void(const std::string&)>& reply);

    /** Bind a Surface::Console spec to a ConCommand of the same name. @p name must be the
     *  lowercased key @p spec is stored under. */
    void RegisterConsoleCommand(const std::string& name, const CommandSpec& spec);

    Runtime& _runtime;
    std::unordered_map<std::string, CommandSpec> _commands;
    /** Lowercased alias -> the lowercased command name that owns it. */
    std::unordered_map<std::string, std::string> _aliases;
    std::vector<std::string> _prefixes{"!", "."};
    /** Command names already reported for a missing HasPermission policy, so the error is
     *  logged once rather than on every attempt. */
    std::unordered_set<std::string> _missingPolicyWarned;
    /** Lowercased command name -> its ConCommand registration, for Surface::Console specs. */
    std::unordered_map<std::string, std::unique_ptr<Engine::ServerCommand>> _consoleCommands;
};

}  // namespace VoltMod::Commands
