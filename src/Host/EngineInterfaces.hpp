#pragma once

#include "Host/HostStart.hpp"

#include <VoltMod/Core/Result.hpp>
#include <format>

namespace VoltMod
{

/** Look @p version up in @p factory. Naming it is the point: a game update is what breaks one. */
template <class Iface>
Status ResolveInterface(Iface*& target, InterfaceFactory factory, const char* version)
{
    target = static_cast<Iface*>(factory(version, nullptr));
    if (target == nullptr)
    {
        return std::unexpected(Error::Engine(std::format("could not find interface: {}", version)));
    }
    return {};
}

}  // namespace VoltMod
