#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/ServerClock.hpp>
#include <globalvars.h>

namespace VoltMod::Sdk
{

namespace
{
/**
 * The live ServerClock's interfaces, published for the free functions below. They take no
 * arguments, so a file-static is the only place a caller-less accessor can find the engine
 * pointer. Set by the ServerClock constructor and cleared by its destructor, so it is never
 * stale past one load cycle.
 */
GameInterfaces* g_clockInterfaces = nullptr;

CGlobalVars* GlobalsFrom(GameInterfaces* interfaces)
{
    return (interfaces && interfaces->Engine) ? interfaces->Engine->GetServerGlobals() : nullptr;
}
}  // namespace

ServerClock::ServerClock(GameInterfaces& interfaces) : _interfaces(interfaces)
{
    g_clockInterfaces = &interfaces;
}

ServerClock::~ServerClock()
{
    if (g_clockInterfaces == &_interfaces)
        g_clockInterfaces = nullptr;
}

CGlobalVars* ServerClock::Globals() const
{
    return GlobalsFrom(&_interfaces);
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

CGlobalVars* GetServerGlobals()
{
    return GlobalsFrom(g_clockInterfaces);
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
