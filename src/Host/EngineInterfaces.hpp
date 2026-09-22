#pragma once

#include <ISmmAPI.h>
#include <VoltMod/Core/Result.hpp>
#include <format>

namespace VoltMod
{

/** Look @p version up in @p factory. Naming it is the point: a game update is what breaks one. */
template <class Iface, class Factory>
Status ResolveInterface(Iface*& target, Factory&& factory, const char* version)
{
    target = static_cast<Iface*>(factory(version));
    if (target == nullptr)
    {
        return std::unexpected(Error::Engine(std::format("could not find interface: {}", version)));
    }
    return {};
}

/** The engine's own interfaces, as @ref ResolveInterface takes them. */
inline auto EngineInterfaces(SourceMM::ISmmAPI* metamod)
{
    return [metamod](const char* version) { return metamod->VInterfaceMatch(metamod->GetEngineFactory(), version, 0); };
}

/** The game server library's interfaces. */
inline auto ServerInterfaces(SourceMM::ISmmAPI* metamod)
{
    return [metamod](const char* version) { return metamod->VInterfaceMatch(metamod->GetServerFactory(), version, 0); };
}

}  // namespace VoltMod
