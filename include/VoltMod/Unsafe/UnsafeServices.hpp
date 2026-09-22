#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief Opt-in access to raw engine interfaces and typed gamedata bindings.
 *
 * Runtime::Initialize populates this before other services. Include `<VoltMod/Unsafe/Api.hpp>` when a
 * plugin needs direct engine access through `runtime.Unsafe`.
 */
struct UnsafeServices
{
    /** Interface pointers populated by Runtime::Initialize. */
    VoltMod::Interfaces Interfaces;
    /** Bindings loaded once by Runtime::Initialize and shared by engine services. */
    VoltMod::Bindings Bindings;
};

}  // namespace VoltMod
