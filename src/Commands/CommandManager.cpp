#include "CommandRouter.hpp"
#include "Commands/CommandSyntax.hpp"

#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Strings.hpp>
#include <VoltMod/Engine/ServerCommand.hpp>
#include <convar.h>
#include <memory>
#include <unordered_map>
#include <utility>

namespace VoltMod
{

/** The engine-facing half: the router plus what only a running server can provide. */
struct CommandManager::Impl
{
    Impl(Policy& policy, Translations& translations, PlayerManager& players, EntitySystem& entities, Messages& messages)
        : Policies(policy),
          Texts(translations),
          Notify(messages),
          Binder(players, policy, entities),
          Router(policy, translations)
    {}

    Policy& Policies;
    Translations& Texts;
    Messages& Notify;
    EngineArgBinder Binder;
    CommandRouter Router;
    /** Lowercased command name -> its ConCommand, for commands that asked for the console. */
    std::unordered_map<std::string, std::unique_ptr<ServerCommand>> ConsoleCommands;
};

CommandManager::CommandManager(Policy& policy, Translations& translations, PlayerManager& players,
                               EntitySystem& entities, Messages& messages)
    : _impl(std::make_unique<Impl>(policy, translations, players, entities, messages))
{}

CommandManager::~CommandManager() = default;

CommandBuilder CommandManager::Add(std::string_view name)
{
    return CommandBuilder(
        [this](CommandDefinition def) {
            const std::string key = Strings::ToLower(def.Name);
            const bool console = def.Console;
            if (_impl->Router.Add(std::move(def)) && console)
                InstallConsoleCommand(key);
        },
        name);
}

void CommandManager::RemoveAll()
{
    _impl->ConsoleCommands.clear();
    _impl->Router.Clear();
}

void CommandManager::InstallConsoleCommand(const std::string& name)
{
    const CommandDefinition* def = _impl->Router.Find(name);
    if (!def)
        return;

    // The console types no prefix, so a derived help line must not claim one.
    const std::string help =
        def->Description.empty() ? _impl->Router.Usage(*def, -1, Origin::Console) : def->Description;

    _impl->ConsoleCommands.emplace(
        name, std::make_unique<ServerCommand>(name.c_str(), help.c_str(), [this, name](const CCommand& args) {
            // Re-resolved per invocation: the command can be unregistered while the ConCommand
            // is still being torn down. `name` is already the canonical key.
            const CommandDefinition* current = _impl->Router.Find(name);
            if (!current || !current->Console)
                return;

            // The engine already split and unquoted the line for us.
            std::vector<std::string> tokens;
            tokens.reserve(static_cast<size_t>(args.ArgC()));
            for (int i = 1; i < args.ArgC(); ++i)
                tokens.emplace_back(args.Arg(i));

            // The console has no chat window to reply into, and no language of its own.
            _impl->Router.Dispatch(*current, nullptr, tokens, Origin::Console, _impl->Binder,
                                   [](const std::string& line) { Log::Info("{}", line); });
        }));
}

bool CommandManager::HandleChatMessage(Player* caller, std::string_view message)
{
    if (!caller)
        return false;

    auto body = CommandSyntax::StripPrefix(message);
    if (!body)
        return false;

    std::vector<std::string> parts = CommandSyntax::Tokenize(*body);
    if (parts.empty())
        return false;

    // A command that did not ask for the chat surface is not typeable in chat at all, which is
    // what keeps an operator command - one with no permission, because the console needs none -
    // out of every player's reach.
    const CommandDefinition* def = _impl->Router.Find(parts.front());
    if (!def || !def->Chat)
        return false;

    const std::span<const std::string> tokens{parts.begin() + 1, parts.end()};
    const int slot = caller->Slot();
    _impl->Router.Dispatch(*def, caller, tokens, Origin::Chat, _impl->Binder, [this, slot](const std::string& line) {
        if (_impl->Policies.Reply)
            _impl->Policies.Reply(slot, line);
        else
            _impl->Notify.Reply(slot, line);
    });

    return true;
}

size_t CommandManager::Count() const
{
    return _impl->Router.Count();
}

std::vector<std::string> CommandManager::CommandsMissingPolicy() const
{
    if (_impl->Policies.HasPermission)
        return {};
    return _impl->Router.NamesWithPermission();
}

}  // namespace VoltMod
