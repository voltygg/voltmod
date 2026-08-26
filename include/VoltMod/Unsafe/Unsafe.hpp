#pragma once

#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>

namespace VoltMod
{

/**
 * @brief The opt-in engine-access tier: raw interface pointers, the gamedata resolutions built
 * from them, and their typed view.
 *
 * Populated by Runtime::Start, not by a constructor - none of the three has anything to take
 * from a sibling service. Declared early in Runtime, ahead of almost every other service, which
 * reads @ref Bindings (or, for diagnostics, @ref GameData) rather than duplicating a resolution.
 * A plugin that pokes at the engine itself includes `<VoltMod/Unsafe/Api.hpp>` and writes
 * `runtime.Unsafe.Bindings`.
 */
struct UnsafeServices
{
    /** Plain interface-pointer holder; populated by Runtime::Start. */
    VoltMod::Interfaces Interfaces;
    /** The raw gamedata resolutions, for diagnostics. Services read @ref Bindings instead. */
    VoltMod::GameData GameData;
    /** The typed view of GameData; bound once by Runtime::Start and handed to every engine
     *  service. */
    VoltMod::Bindings Bindings;
};

}  // namespace VoltMod
