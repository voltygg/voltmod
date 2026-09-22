#pragma once

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/**
 * @brief Owns typed commands for chat and the server console.
 *
 * A handler's parameters define how its arguments are parsed:
 *
 * @code
 * commands.Add("slap")
 *     .Permission("admin.control")
 *     .Run([&](Caller c, Args::Target t, Args::Opt<Args::Int> damage) -> Result<Reply> {
 *         return c.Ok("cmd.slapped", {{"name", t.Value->Name()}});
 *     });
 * @endcode
 *
 * `Run()` installs the command for the manager's lifetime. Handlers may capture plugin state because
 * the framework removes every command before destroying the plugin.
 *
 * Dispatch resolves aliases, authorizes the command, and binds every argument before calling the
 * handler (see @ref ArgKind). Invalid input gets a localized reply. Permissioned commands are denied
 * without `Policy::HasPermission`; replies use `Policy::Reply` or fall back to `Messages::Reply`.
 *
 * `Anywhere()` also exposes the command as a tier1 ConCommand, removed with the command. When the
 * server runs it, the handler has no player and the reply prints to the server console; typed in
 * a player's own console it runs as that player, like chat. A `ServerOnly()` command ignores players.
 */
class CommandManager
{
public:
    /** Every service dispatch reaches, taken directly: this class does not know the runtime.
     *  All five must outlive the manager, which the runtime's declaration order guarantees. */
    CommandManager(Policy& policy, Translations& translations, PlayerManager& players, EntitySystem& entities,
                   Messages& messages);
    ~CommandManager();
    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;

    /** Attach to @p host, which hands out command names for the whole process. Called before the
     *  plugin is constructed, so every command added in Load is registered with it. A null host
     *  keeps the names local to this plugin. */
    void Attach(IHost* host);

    /** Start describing a command. Finish with `.Run(handler)`, which installs it. */
    CommandBuilder Add(std::string_view name);

    /** Dispatch @p message when it starts with the `!` command prefix.
     *  @return true when it was a command, so the chat line should not be shown. */
    bool HandleChatMessage(Player* caller, std::string_view message);

    /** True when @p message invokes a command another plugin registered, which that plugin
     *  answers; this one should pass the line on without filtering it as chat. */
    bool IsForeign(std::string_view message) const;

    size_t Count() const;

    /** Unregister every command and its ConCommand. Called by the framework on the unload
     *  path, before the plugin's own state goes away; plugins do not call this. */
    void RemoveAll();

    /** Names of registered commands that declare a permission while no `HasPermission` policy
     *  is installed. Every one of them will be denied; the framework reports this after Load
     *  so the misconfiguration shows up in the load summary instead of the first time a player
     *  tries the command. */
    std::vector<std::string> CommandsMissingPolicy() const;

private:
    /** Bind the already-registered @p def to a tier1 ConCommand of the same name. */
    void InstallConsoleCommand(const CommandDefinition& def);
    /** Send a command's reply line to @p slot through `Policy::Reply`, else chat. */
    void ReplyToPlayer(int slot, const std::string& line);

    /** Holds the engine-free router, the engine-backed argument binder, and the ConCommands.
     *  Hidden so this header does not reach under src/. */
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace VoltMod
