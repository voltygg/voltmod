#pragma once

#include <VoltMod/Commands/CommandBuilder.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
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
 * @brief Registers commands and dispatches chat and console invocations to them.
 *
 * Registration is fluent and the handler's signature is the argument spec:
 *
 * @code
 * commands.Add("slap")
 *     .Permission("s")
 *     .Run([&](Caller c, Args::Target t, Args::Opt<Args::Int> damage) -> Result<Reply> {
 *         return c.Ok("cmd.slapped", {{"name", t.Value->Name()}});
 *     });
 * @endcode
 *
 * A command lives as long as the manager, so registration hands nothing back to hold. Handlers
 * routinely capture plugin state; @ref MetamodPlugin drops every command before `OnUnload`, so
 * they stop before that state does.
 *
 * The pipeline per invocation: prefix match -> name or alias lookup -> `Policy::Authorize` for
 * the command's permission (which denies when no `HasPermission` policy is installed) -> arity
 * -> typed argument binding (targets, durations, SteamIDs - see @ref ArgKind) -> handler ->
 * the reply through `Policy::Reply`, falling back to `Messages::Reply`. Handlers only run with
 * fully-resolved, validated arguments; every earlier failure replies with a localized message
 * and stops.
 *
 * `Console()` additionally registers a tier1 ConCommand of the same name, running the same
 * binding and handler with no caller and printing its reply to the console. It is removed with
 * the command.
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

    /** Start describing a command. Finish with `.Run(handler)`, which installs it. */
    [[nodiscard]] CommandBuilder Add(std::string_view name);

    /** Dispatch @p message when it starts with a command prefix (`!` or `.`).
     *  @return true when it was a command, so the chat line should not be shown. */
    bool HandleChatMessage(Player* caller, std::string_view message);

    size_t Count() const;

    /** Unregister every command and its ConCommand. Called by @ref MetamodPlugin on the unload
     *  path, before the plugin's own state goes away; plugins do not call this. */
    void RemoveAll();

    /** Names of registered commands that declare a permission while no `HasPermission` policy
     *  is installed. Every one of them will be denied; MetamodPlugin reports this after OnLoad
     *  so the misconfiguration shows up in the load summary instead of the first time a player
     *  tries the command. */
    std::vector<std::string> CommandsMissingPolicy() const;

private:
    /** Bind the already-registered command @p name to a tier1 ConCommand of the same name. */
    void InstallConsoleCommand(const std::string& name);

    /** Holds the engine-free router, the engine-backed argument binder, and the ConCommands.
     *  Hidden so this header does not reach under src/. */
    struct Impl;
    std::unique_ptr<Impl> _impl;
};

}  // namespace VoltMod
