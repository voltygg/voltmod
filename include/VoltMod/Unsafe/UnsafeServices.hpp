#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief Opt-in access to raw engine interfaces and typed gamedata bindings.
 *
 * The plugin module fills this before the runtime and owns it, so every engine service is built
 * with its interfaces already resolved. Include `<VoltMod/Unsafe/Api.hpp>` when a plugin needs
 * direct engine access through `runtime.Unsafe`.
 */
struct UnsafeServices
{
    VoltMod::Interfaces Interfaces;
    VoltMod::Bindings Bindings;
    /** Why gamedata did not bind; success when it did. */
    Status GameData;
};

}  // namespace VoltMod
