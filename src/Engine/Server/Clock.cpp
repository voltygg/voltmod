#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Server/Clock.hpp>
#include <globalvars.h>

namespace VoltMod
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

std::string_view Clock::MapName() const
{
    auto* globals = Globals();
    const char* name = globals ? globals->mapname.ToCStr() : nullptr;
    return name ? std::string_view(name) : std::string_view{};
}

}  // namespace VoltMod
