#pragma once

#include "Engine/Memory/ModuleImage.hpp"

#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <optional>

namespace VoltMod
{

/**
 * Find `className`'s primary vtable in loaded `moduleName`, or nullptr. Uses compiler metadata:
 * MSVC RTTI on Windows (top-level, non-template classes only) and the Itanium `_ZTV` symbol on
 * Linux (unavailable in a stripped library).
 */
void* FindVirtualTable(const char* moduleName, const char* className);

/** Find @p className's primary vtable in an already-located @p image, or nullptr. */
void* FindVirtualTableIn(const ModuleImage& image, const char* className);

/** Location of a virtual function in an object's vtables. See @ref FindVTableSlot. */
struct VTableSlot
{
    void* Table = nullptr;  ///< the vtable holding the function, as a hook binds to it
    int Index = 0;          ///< slot within that table
    int BaseOffset = 0;     ///< bytes from the object to the subobject that owns Table
};

/**
 * Find @p function's slot in @p table, or nothing.
 *
 * @param maxSlots how far to read before giving up. A caller holding a real class table can afford
 *                 @ref MaxVtableIndex; a blind walk over an unknown object must stay small.
 */
std::optional<int> FindSlotInTable(void* table, const void* function, const OriginalVfn& originalOf = {},
                                   int maxSlots = 16);

/**
 * Locate @p function among the vtables carried by a live @p instance, including secondary bases
 * such as `CServerSideClient::FilterMessage` that @ref FindVirtualTable cannot return.
 *
 * The blind walk tries the first eight object words as vptrs and each table until a non-code slot.
 * These are foreign-memory reads, so pass a real engine object at least eight words long whose
 * vtables are followed by readable non-executable data, not a small stack struct.
 *
 * @return the slot, or nothing. `BaseOffset` lets a secondary-table handler recover the object.
 */
std::optional<VTableSlot> FindVTableSlot(const void* instance, const void* function,
                                         const OriginalVfn& originalOf = {});

}  // namespace VoltMod
