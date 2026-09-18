#pragma once

#include <ISmmPlugin.h>

#include <VoltMod/Engine/EngineTypes.hpp>

// KHook's dispatch pointer, declared for every translation unit that installs a hook.
//
// Each module carries its own copy: KHook's entry points are defined in its header and resolve
// this against the module they were compiled into. The host defines and fills its own through
// Metamod; a plugin library defines its own in VOLTMOD_PLUGIN and Plugin::Attach seeds it from
// IHost::Detours(). The declaration lives in Engine because vtable hooks must not depend on the
// composition root.
namespace KHook
{
extern KHook::IKHook* __exported__khook;
}

namespace VoltMod
{

/** What @p vtable's @p index held before a hook patched it, or nullptr. */
inline const void* ReadOriginalSlot(void** vtable, int index)
{
    // The pointer arrives at load; before then there is nothing to query.
    return KHook::__exported__khook ? KHook::FindOriginalVirtual(vtable, index) : nullptr;
}

}  // namespace VoltMod
