#pragma once

#include <ISmmPlugin.h>
#include <VoltMod/Engine/EngineTypes.hpp>

// Declarations for the Metamod globals used by plugin translation units, including KHook's
// dispatch pointer. VOLTMOD_PLUGIN / PLUGIN_EXPOSE provide the definitions and the framework's
// single PLUGIN_GLOBALVARS() call. Include this header instead of repeating the macro.
//
// The declarations live in Engine because vtable hooks must not depend on the composition root.
PLUGIN_GLOBALVARS();

namespace VoltMod
{

/** What @p vtable's @p index held before a hook patched it, or nullptr. */
inline const void* ReadOriginalSlot(void** vtable, int index)
{
    // Metamod sets the dispatcher during Load; before then there is nothing to query.
    return KHook::__exported__khook ? KHook::FindOriginalVirtual(vtable, index) : nullptr;
}

}  // namespace VoltMod
