#pragma once

#include <ISmmPlugin.h>

// Externs for the SourceHook globals every plugin TU references (SH_* macros,
// RETURN_META); the definitions come from VOLTMOD_PLUGIN / PLUGIN_EXPOSE. Pure
// externs, so a TU repeating PLUGIN_GLOBALVARS() itself is harmless.
//
// In Core rather than beside the plugin base: hooking is an engine-level concern, and
// the Sdk services that install vtable hooks must not depend on the composition root.
PLUGIN_GLOBALVARS();
