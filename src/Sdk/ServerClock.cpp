#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/ServerClock.hpp>
#include <globalvars.h>

namespace CS2Kit::Sdk
{

CGlobalVars* GetServerGlobals()
{
    auto* services = Core::EngineOrNull();
    if (!services || !services->Interfaces.Engine)
        return nullptr;
    return services->Interfaces.Engine->GetServerGlobals();
}

int ServerTick()
{
    auto* globals = GetServerGlobals();
    return globals ? globals->tickcount : 0;
}

float ServerTime()
{
    auto* globals = GetServerGlobals();
    return globals ? globals->curtime : 0.0f;
}

}  // namespace CS2Kit::Sdk
