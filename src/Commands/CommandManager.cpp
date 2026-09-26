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

CommandManager::CommandManager(Policy& policy, Translations& translations, PlayerManager& players,
                               EntitySystem& entities, Messages& messages)
    : _policy(policy),
      _players(players),
      _messages(messages),
      _binder(std::make_unique<EngineArgBinder>(players, policy, entities)),
      _router(std::make_unique<CommandRouter>(policy, translations))
{}

CommandManager::~CommandManager() = default;

void CommandManager::Attach(IHost* host)
{
    _router->Attach(host);
}

CommandBuilder CommandManager::Add(std::string_view name)
{
    return CommandBuilder(
        [this](CommandDefinition def) {
            const CommandDefinition* added = _router->Add(std::move(def));
            if (added && added->Access != CommandAccess::Players)
            {
                InstallConsoleCommand(*added);
            }
        },
        name);
}

void CommandManager::RemoveAll()
{
    _consoleCommands.clear();
    _router->Clear();
}

void CommandManager::InstallConsoleCommand(const CommandDefinition& def)
{
    const std::string name = Strings::ToLower(def.Name);
    const std::string help = def.Description.empty() ? _router->Usage(def, -1, Origin::Console) : def.Description;

    ServerCommand::Handler run = [this, name](const CCommand& args, int slot) {
        // Shutdown can unregister the command before the engine drops it.
        const CommandDefinition* current = _router->Find(name);
        if (!current)
        {
            return;
        }

        std::vector<std::string> tokens;
        tokens.reserve(static_cast<size_t>(args.ArgC()));
        for (int i = 1; i < args.ArgC(); ++i)
        {
            tokens.emplace_back(args.Arg(i));
        }

        if (slot >= 0)
        {
            if (Player* player = _players.Get(slot))
            {
                _router->Dispatch(*current, player, tokens, Origin::Console, *_binder,
                                  [this, slot](const std::string& line) { ReplyToPlayer(slot, line); });
            }
            return;
        }

        _router->Dispatch(*current, nullptr, tokens, Origin::Console, *_binder,
                          [](const std::string& line) { Log::Info("{}", line); });
    };

    const bool playersCanRun = def.Access == CommandAccess::Anywhere;
    _consoleCommands.try_emplace(name, name, help, std::move(run), playersCanRun);
}

void CommandManager::ReplyToPlayer(int slot, const std::string& line)
{
    if (_policy.Reply)
    {
        _policy.Reply(slot, line);
    }
    else
    {
        _messages.Send(slot, line);
    }
}

bool CommandManager::HandleChatMessage(Player* caller, std::string_view message)
{
    if (!caller)
    {
        return false;
    }

    auto body = CommandSyntax::StripPrefix(message);
    if (!body)
    {
        return false;
    }

    std::vector<std::string> parts = CommandSyntax::Tokenize(*body);
    if (parts.empty())
    {
        return false;
    }

    const CommandDefinition* def = _router->Find(parts.front());
    if (!def || def->Access == CommandAccess::ServerOnly)
    {
        return false;
    }

    const std::span<const std::string> tokens{parts.begin() + 1, parts.end()};
    const int slot = caller->Slot();
    _router->Dispatch(*def, caller, tokens, Origin::Chat, *_binder,
                      [this, slot](const std::string& line) { ReplyToPlayer(slot, line); });

    return true;
}

bool CommandManager::IsForeign(std::string_view message) const
{
    auto body = CommandSyntax::StripPrefix(message);
    if (!body)
    {
        return false;
    }

    std::vector<std::string> parts = CommandSyntax::Tokenize(*body);
    return !parts.empty() && _router->IsForeign(parts.front());
}

size_t CommandManager::Count() const
{
    return _router->Count();
}

std::vector<std::string> CommandManager::CommandsMissingPolicy() const
{
    if (_policy.HasPermission)
    {
        return {};
    }
    return _router->NamesWithPermission();
}

}  // namespace VoltMod
