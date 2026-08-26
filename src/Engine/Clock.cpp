#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <globalvars.h>

namespace VoltMod::Engine
{

Clock::Clock(Interfaces& interfaces) : _interfaces(interfaces) {}

CGlobalVars* Clock::Globals() const
{
    return _interfaces.Engine ? _interfaces.Engine->GetServerGlobals() : nullptr;
}

int Clock::Tick() const
{
    auto* globals = Globals();
    return globals ? globals->tickcount : 0;
}

float Clock::Time() const
{
    auto* globals = Globals();
    return globals ? globals->curtime : 0.0f;
}

}  // namespace VoltMod::Engine
