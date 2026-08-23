#include <CS2Kit/Commands/CommandAccess.hpp>
#include <CS2Kit/Core/ActiveService.hpp>

namespace CS2Kit::Commands
{

void SetActiveCommands(CommandManager* commands)
{
    Core::ActiveService<CommandManager>::Set(commands);
}

CommandManager& Manager()
{
    return Core::ActiveService<CommandManager>::Get();
}

CommandManager* ManagerOrNull()
{
    return Core::ActiveService<CommandManager>::GetOrNull();
}

}  // namespace CS2Kit::Commands
