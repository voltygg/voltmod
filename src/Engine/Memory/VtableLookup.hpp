#pragma once

#include "Engine/Memory/ModuleImage.hpp"

#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <optional>
#include <span>

namespace VoltMod
{

/**
 * Find `className`'s primary vtable in loaded `moduleName`, or nullptr. Uses compiler metadata:
 * MSVC RTTI on Windows (top-level, non-template classes only), and on Linux the Itanium `_ZTV`
 * symbol, or the Itanium RTTI when the library hides that symbol.
 */
void* FindVirtualTable(const char* moduleName, const char* className);

/** Find @p className's primary vtable in an already-located @p image, or nullptr. */
void* FindVirtualTableIn(const ModuleImage& image, const char* className);

/** Find @p className's primary vtable through Itanium RTTI in readable @p ranges, or nullptr. */
void* FindVirtualTableByTypeName(std::span<const ScanRange> ranges, const char* className);

/** @p function's slot in @p table, within @p maxSlots and before the first non-code slot. Sees
 *  through other plugins' hooks. */
std::optional<int> FindSlotInTable(void* table, const void* function, const OriginalVfn& originalOf, int maxSlots);

}  // namespace VoltMod
