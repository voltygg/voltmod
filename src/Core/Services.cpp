#include <CS2Kit/Core/ActiveService.hpp>
#include <CS2Kit/Core/Services.hpp>

namespace CS2Kit::Core
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
    ActiveService<Services>::Set(services);
}

Services& Engine()
{
    return ActiveService<Services>::Get();
}

Services* EngineOrNull()
{
    return ActiveService<Services>::GetOrNull();
}

}  // namespace CS2Kit::Core
