#include <CS2Kit/App/Services.hpp>
#include <CS2Kit/Commands/CommandAccess.hpp>
#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Menu/MenuAccess.hpp>
#include <CS2Kit/Players/Roster.hpp>

namespace CS2Kit::App
{

Services::Services()
{
    // Each layer gets its own accessor, so nothing below this one has to reach the hub.
    Core::SetActiveCoreServices(&Core);
    Utils::SetActiveUtilsServices(&Utils);
    Sdk::SetActiveSdkServices(&Sdk);
    Players::SetActiveRoster(&Players);
    Commands::SetActiveCommands(&Commands);
    Menu::SetActiveMenus(&Menus);
}

Services::~Services()
{
    Menu::SetActiveMenus(nullptr);
    Commands::SetActiveCommands(nullptr);
    Players::SetActiveRoster(nullptr);
    Sdk::SetActiveSdkServices(nullptr);
    Utils::SetActiveUtilsServices(nullptr);
    Core::SetActiveCoreServices(nullptr);
}

void SetActiveServices(Services* services)
{
    Core::ActiveService<Services>::Set(services);
}

Services& Engine()
{
    return Core::ActiveService<Services>::Get();
}

Services* EngineOrNull()
{
    return Core::ActiveService<Services>::GetOrNull();
}

}  // namespace CS2Kit::App
