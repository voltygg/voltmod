#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief Opt-in access to raw engine interfaces and typed gamedata bindings.
 *
 * Runtime::Start populates this before other services. Include `<VoltMod/Unsafe/Api.hpp>` when a
 * plugin needs direct engine access through `runtime.Unsafe`.
 */
struct UnsafeServices
{
    /** Interface pointers populated by Runtime::Start. */
    VoltMod::Interfaces Interfaces;
    /** Bindings loaded once by Runtime::Start and shared by engine services. */
    VoltMod::Bindings Bindings;
};

}  // namespace VoltMod
