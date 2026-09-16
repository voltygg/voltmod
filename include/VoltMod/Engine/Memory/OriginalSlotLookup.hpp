#pragma once

#include <functional>

namespace VoltMod
{

/**
 * Reads the value that @p vtable's @p index held before a hook patched it, or nullptr. An empty
 * lookup reads the current slot. The injected lookup keeps vtable searches SDK-free; the default
 * implementation is `ReadOriginalSlot` in MetamodGlobals.hpp.
 */
using OriginalSlotLookup = std::function<const void*(void** vtable, int index)>;

}  // namespace VoltMod
