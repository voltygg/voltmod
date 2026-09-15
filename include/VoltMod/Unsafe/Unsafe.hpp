#pragma once

#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief The opt-in engine-access tier: raw interface pointers and the typed gamedata bindings.
 *
 * Populated by Runtime::Start, not by a constructor. Declared early in Runtime, ahead of almost
 * every other service, which reads @ref Bindings rather than resolving gamedata itself. A plugin
 * that pokes at the engine itself includes `<VoltMod/Unsafe/Api.hpp>` and writes
 * `runtime.Unsafe.Bindings`.
 */
struct UnsafeServices
{
    /** Plain interface-pointer holder; populated by Runtime::Start. */
    VoltMod::Interfaces Interfaces;
    /** Loaded from gamedata once by Runtime::Start and handed to every engine service. */
    VoltMod::Bindings Bindings;
};

}  // namespace VoltMod
