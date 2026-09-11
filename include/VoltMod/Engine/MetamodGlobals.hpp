#pragma once

#include <ISmmPlugin.h>
#include <VoltMod/Engine/EngineTypes.hpp>

// Externs for the Metamod globals every plugin TU references, KHook's dispatch pointer among
// them; the definitions come from VOLTMOD_PLUGIN / PLUGIN_EXPOSE. This is the framework's single
// PLUGIN_GLOBALVARS() call - a translation unit that needs the globals includes this header
// rather than repeating the macro.
//
// In Engine rather than beside the plugin base: hooking is an engine-level concern, and
// the hook services that install vtable hooks must not depend on the composition root.
PLUGIN_GLOBALVARS();

namespace VoltMod
{

/** What @p vtable's @p index held before a hook patched it, or nullptr. */
inline const void* OriginalVfnPtr(void** vtable, int index)
{
    // Metamod hands the plugin its dispatcher during Load; before that there is nothing to ask.
    return KHook::__exported__khook ? KHook::FindOriginalVirtual(vtable, index) : nullptr;
}

}  // namespace VoltMod
