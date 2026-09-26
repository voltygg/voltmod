#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <khook.hpp>

// KHook's dispatch pointer, declared for every translation unit that installs a hook.
//
// Each module carries its own copy, because KHook's header forwards every call through it. The
// host fills its own from the loader; a plugin defines one in its generated entry point and seeds it from
// IHost::HookDispatcher(). It lives in Engine because vtable hooks must not depend on the
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
