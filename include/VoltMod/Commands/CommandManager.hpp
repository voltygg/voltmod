#pragma once

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace VoltMod
{

/**
 * @brief Typed commands for chat and the server console.
 *
 * The handler's parameters are the argument spec; dispatch authorizes, binds and replies:
 *
 * @code
 * commands.Add("slap")
 *     .Permission("admin.control")
 *     .Run([&](Caller c, Args::Target t, Args::Opt<Args::Int> damage) -> Result<Reply> {
 *         return c.Ok("cmd.slapped", {{"name", t.Value->Name()}});
 *     });
 * @endcode
 *
 * Commands live until the plugin unloads; the framework removes them before the plugin's state.
 * `Anywhere()` and `ServerOnly()` also register a console command (see @ref commands_guide).
 */
class CommandManager
{
public:
    /** Every argument must outlive the manager. */
    CommandManager(Policy& policy, Translations& translations, PlayerManager& players, EntitySystem& entities,
                   Messages& messages);
    ~CommandManager();
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    /** Share command names with the other plugins on @p host. Null keeps them local. Framework only. */
    void Attach(IHost* host);

    /** Start a command; `.Run(handler)` installs it. */
    CommandBuilder Add(std::string_view name);

    /** Run @p message when it is a `!` command. Framework only.
     *  @return true when it was one, so chat hides the line. */
    bool HandleChatMessage(Player* caller, std::string_view message);

    /** True when @p message is another plugin's command, which chat must pass on unfiltered. */
    bool IsForeign(std::string_view message) const;

    size_t Count() const;

    /** Remove every command. Framework only, on unload. */
    void RemoveAll();

    /** Commands that need a permission while no `HasPermission` policy is installed, so every call
     *  is denied. Reported after Load. */
    std::vector<std::string> CommandsMissingPolicy() const;

private:
    void InstallConsoleCommand(const CommandDefinition& def);
    /** Reply through `Policy::Reply`, else chat. */
    void ReplyToPlayer(int slot, const std::string& line);

    Policy& _policy;
    PlayerManager& _players;
    Messages& _messages;
    std::unique_ptr<EngineArgBinder> _binder;
    std::unique_ptr<CommandRouter> _router;
    /** By lowercased name. Declared after the router, which their handlers use. */
    std::unordered_map<std::string, ServerCommand> _consoleCommands;
};

}  // namespace VoltMod
