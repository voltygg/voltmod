#pragma once

#include <functional>

namespace VoltMod
{

/**
 * What @p vtable's @p index held before a hook patched it, or nullptr; empty reads the slot as it
 * stands. Injected to keep vtable searches SDK-free; `ReadOriginalSlot` in MetamodGlobals.hpp is
 * the one implementation, wrapping KHook's `FindOriginalVirtual`.
 */
using OriginalSlotLookup = std::function<const void*(void** vtable, int index)>;

}  // namespace VoltMod
