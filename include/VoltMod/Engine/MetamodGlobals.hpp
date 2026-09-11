#pragma once

#include <ISmmPlugin.h>

// Externs for the SourceHook globals every plugin TU references (SH_* macros,
// RETURN_META); the definitions come from VOLTMOD_PLUGIN / PLUGIN_EXPOSE. This is
// the framework's single PLUGIN_GLOBALVARS() call - a translation unit that needs
// the globals includes this header rather than repeating the macro.
//
// In Engine rather than beside the plugin base: hooking is an engine-level concern, and
// the hook services that install vtable hooks must not depend on the composition root.
PLUGIN_GLOBALVARS();

namespace VoltMod
{

/** What a vtable entry held before a hook patched it, or nullptr. Pass where `originalOf` is asked for. */
inline const void* OriginalVfnPtr(void* entry)
{
    return g_SHPtr ? g_SHPtr->GetOrigVfnPtrEntry(entry) : nullptr;
}

}  // namespace VoltMod
