#pragma once

#include "Engine/Memory/LoadedModule.hpp"

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Memory/OriginalVfn.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace VoltMod
{

/**
 * Find `className`'s primary vtable in loaded `moduleName`, or nullptr. Uses compiler metadata:
 * MSVC RTTI on Windows (top-level, non-template classes only), and on Linux the Itanium `_ZTV`
 * symbol, or the Itanium RTTI when the module hides that symbol.
 */
void* FindVirtualTable(const char* moduleName, const char* className);

/** Find @p className's primary vtable in the already-located module @p loaded, or nullptr. */
void* FindVirtualTableIn(const LoadedModule& loaded, const char* className);

/** Find @p className's primary vtable through Itanium RTTI in readable @p ranges, or nullptr. */
void* FindVirtualTableByTypeName(std::span<const ScanRange> ranges, const char* className);

/** @p function's slot in @p table, within @p maxSlots and before the first non-code slot. Sees
 *  through other plugins' hooks. */
std::optional<int> FindSlotInTable(void* table, const void* function, const OriginalVfn& originalOf, int maxSlots);

/** Whether @p object is readable and its vptr is @p table, so it really is that class. */
bool IsInstanceOf(const void* object, const void* table);

/** A base class inside a complete object: where it starts, and its own vtable when it has one. */
struct BaseSubobject
{
    int Offset = 0;
    void* Table = nullptr;
};

/**
 * Find the base @p baseName inside @p className in @p loaded, through RTTI.
 *
 * @return NotFound when either class is missing, Invalid when the base is in the class more than
 *         once, Unsupported for a virtual base. Table is null when the base has no vtable.
 */
Result<BaseSubobject> FindBaseIn(const LoadedModule& loaded, const char* className, const char* baseName);

/**
 * The vptr values of Itanium `__si_class_type_info` (one base) and `__vmi_class_type_info`
 * (several). Zero when unknown; each typeinfo is then told apart by its shape.
 */
struct TypeInfoKinds
{
    uintptr_t SingleBase = 0;
    uintptr_t MultipleBases = 0;
};

/** Offset of the base @p baseName in the class the Itanium @p typeInfo describes. Errors as @ref FindBaseIn. */
Result<int> FindBaseOffsetByTypeInfo(const void* typeInfo, const char* baseName, const TypeInfoKinds& kinds);

/** The one vtable in @p ranges with typeinfo @p typeInfo, offset-to-top @p offsetToTop and code in its
 *  first slot; null when there is none or more than one. */
void* FindVirtualTableByTypeInfo(std::span<const ScanRange> ranges, const void* typeInfo, intptr_t offsetToTop);

#ifdef _WIN32
/** A PE module's span and the sections its RTTI lives in; separate so tests can walk a fake module. */
struct PeRtti
{
    const uint8_t* Base = nullptr;
    size_t Size = 0;
    ScanRange Data{};          ///< `.data`: type descriptors.
    ScanRange ReadOnlyData{};  ///< `.rdata`: object locators, class hierarchies, vtables.
};

/** @ref FindVirtualTableIn over already-located sections. */
void* FindVirtualTableInRtti(const PeRtti& rtti, const char* className);

/** @ref FindBaseIn over already-located sections. */
Result<BaseSubobject> FindBaseInRtti(const PeRtti& rtti, const char* className, const char* baseName);
#endif

}  // namespace VoltMod
