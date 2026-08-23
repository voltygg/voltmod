#include <CS2Kit/App/Services.hpp>
#include <CS2Kit/Core/ActiveService.hpp>

namespace CS2Kit::App
{

Services::Services()
{
    // The Sdk layer has its own accessor so nothing under Players has to reach the hub.
    Sdk::SetActiveSdkServices(&Sdk);
}

Services::~Services()
{
    Sdk::SetActiveSdkServices(nullptr);
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
