#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/ServerClock.hpp>
#include <globalvars.h>

namespace VoltMod::Sdk
{

CGlobalVars* GetServerGlobals()
{
    auto* services = VoltMod::Detail::RtOrNull();
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

}  // namespace VoltMod::Sdk
