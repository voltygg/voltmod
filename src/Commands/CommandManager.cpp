#include "Commands/CommandRouter.hpp"
#include "Commands/CommandSyntax.hpp"

#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/Server/ServerCommand.hpp>
#include <convar.h>
#include <memory>
#include <unordered_map>
#include <utility>

namespace VoltMod
{

/** Engine-facing command state. */
struct CommandManager::Impl
{
    Impl(Policy& policy, Translations& translations, PlayerManager& players, EntitySystem& entities, Messages& messages)
        : Policies(policy),
          Texts(translations),
          Players(players),
          Notify(messages),
          Binder(players, policy, entities),
          Router(policy, translations)
    {}

    Policy& Policies;
    Translations& Texts;
    PlayerManager& Players;
    Messages& Notify;
    EngineArgBinder Binder;
    CommandRouter Router;
    /** Lowercased names of commands exposed to the console. */
    std::unordered_map<std::string, std::unique_ptr<ServerCommand>> ConsoleCommands;
};

CommandManager::CommandManager(Policy& policy, Translations& translations, PlayerManager& players,
                               EntitySystem& entities, Messages& messages)
    : _impl(std::make_unique<Impl>(policy, translations, players, entities, messages))
{}

CommandManager::~CommandManager() = default;

void CommandManager::Attach(IHost* host)
{
    _impl->Router.Attach(host);
}

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

    // Console usage has no chat prefix.
    const std::string help =
        def->Description.empty() ? _impl->Router.Usage(*def, -1, Origin::Console) : def->Description;

    ServerCommand::PlayerHandler run = [this, name](const CCommand& args, int slot) {
        // Resolve on each call because shutdown can unregister the command first.
        const CommandDefinition* current = _impl->Router.Find(name);
        if (!current || !current->Console)
            return;

        std::vector<std::string> tokens;
        tokens.reserve(static_cast<size_t>(args.ArgC()));
        for (int i = 1; i < args.ArgC(); ++i)
            tokens.emplace_back(args.Arg(i));

        // A player typing it in their own console is that player, never the server.
        if (slot >= 0)
        {
            if (Player* player = _impl->Players.Get(slot))
                _impl->Router.Dispatch(*current, player, tokens, Origin::Console, _impl->Binder,
                                       [this, slot](const std::string& line) { ReplyToPlayer(slot, line); });
            return;
        }

        // Console replies use the server language.
        _impl->Router.Dispatch(*current, nullptr, tokens, Origin::Console, _impl->Binder,
                               [](const std::string& line) { Log::Info("{}", line); });
    };

    // A console-only command stays with the server; one players may type in chat, they may type in their console.
    std::unique_ptr<ServerCommand> command;
    if (def->Chat)
    {
        command = std::make_unique<ServerCommand>(name, help, std::move(run));
    }
    else
    {
        ServerCommand::Handler runAsServer = [run](const CCommand& args) { run(args, -1); };
        command = std::make_unique<ServerCommand>(name, help, std::move(runAsServer));
    }
    _impl->ConsoleCommands.emplace(name, std::move(command));
}

void CommandManager::ReplyToPlayer(int slot, const std::string& line)
{
    if (_impl->Policies.Reply)
        _impl->Policies.Reply(slot, line);
    else
        _impl->Notify.Reply(slot, line);
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

    // Commands without a chat surface remain console-only.
    const CommandDefinition* def = _impl->Router.Find(parts.front());
    if (!def || !def->Chat)
        return false;

    const std::span<const std::string> tokens{parts.begin() + 1, parts.end()};
    const int slot = caller->Slot();
    _impl->Router.Dispatch(*def, caller, tokens, Origin::Chat, _impl->Binder,
                           [this, slot](const std::string& line) { ReplyToPlayer(slot, line); });

    return true;
}

bool CommandManager::IsForeign(std::string_view message) const
{
    auto body = CommandSyntax::StripPrefix(message);
    if (!body)
        return false;

    std::vector<std::string> parts = CommandSyntax::Tokenize(*body);
    return !parts.empty() && _impl->Router.IsForeign(parts.front());
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
