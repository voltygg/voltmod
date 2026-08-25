#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/ServerClock.hpp>
#include <globalvars.h>

namespace VoltMod::Sdk
{

ServerClock::ServerClock(GameInterfaces& interfaces) : _interfaces(interfaces) {}

CGlobalVars* ServerClock::Globals() const
{
    return _interfaces.Engine ? _interfaces.Engine->GetServerGlobals() : nullptr;
}

int ServerClock::Tick() const
{
    auto* globals = Globals();
    return globals ? globals->tickcount : 0;
}

float ServerClock::Time() const
{
    auto* globals = Globals();
    return globals ? globals->curtime : 0.0f;
}

}  // namespace VoltMod::Sdk
