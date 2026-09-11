#pragma once

#include <functional>

namespace VoltMod
{

/**
 * What a vtable entry held before a hook patched it, or nullptr; empty reads the entry as it stands.
 * Injected to keep vtable searches SDK-free; `OriginalVfnPtr` in MetamodGlobals.hpp is the one
 * implementation, wrapping SourceHook's `GetOrigVfnPtrEntry`.
 */
using OriginalVfn = std::function<const void*(void* entry)>;

}  // namespace VoltMod
