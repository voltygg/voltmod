#pragma once

#include "Engine/Memory/Image.hpp"

#include <VoltMod/Core/Result.hpp>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace VoltMod
{

/**
 * Find `className`'s primary vtable in loaded `moduleName`, or nullptr. It uses compiler metadata:
 * MSVC RTTI on Windows (top-level, non-template classes only), and on Linux the Itanium `_ZTV`
 * symbol, or the Itanium RTTI when the module hides that symbol.
 */
void* FindVirtualTable(std::string_view moduleName, std::string_view className);

void* FindVirtualTableIn(const Image& module, std::string_view className);

void* FindVirtualTableByTypeName(std::span<const ScanRange> ranges, std::string_view className);

std::string LengthPrefixedName(std::string_view className);

bool IsInstanceOf(const void* object, const void* table);

struct BaseSubobject
{
    int Offset = 0;
    void* Table = nullptr;
};

/**
 * Find base @p baseName inside @p className in @p module through RTTI.
 *
 * @return NotFound when either class is missing, Invalid when the base is in the class more than
 *         once, Unsupported for a virtual base. Table is null when the base has no vtable.
 */
Result<BaseSubobject> FindBaseIn(const Image& module, std::string_view className, std::string_view baseName);

/**
 * The vptr values of Itanium `__si_class_type_info` (one base) and `__vmi_class_type_info`
 * (several). Zero when unknown; each typeinfo is then told apart by its shape.
 */
struct TypeInfoKinds
{
    uintptr_t SingleBase = 0;
    uintptr_t MultipleBases = 0;
};

/**
 * Find @p baseName in the class described by Itanium @p typeInfo.
 * Uses the same NotFound, Invalid, and Unsupported errors as @ref FindBaseIn.
 */
Result<int> FindBaseOffsetByTypeInfo(const void* typeInfo, std::string_view baseName, const TypeInfoKinds& kinds);

/**
 * Find the single vtable with @p typeInfo and @p offsetToTop whose first slot holds code.
 * Returns nullptr when no candidate exists or the metadata is ambiguous.
 */
void* FindVirtualTableByTypeInfo(std::span<const ScanRange> ranges, const void* typeInfo, intptr_t offsetToTop);

#ifdef _WIN32
/** A PE module's span and RTTI sections. */
struct PeRtti
{
    const uint8_t* Base = nullptr;
    size_t Size = 0;
    ScanRange Data{};          ///< `.data`: type descriptors.
    ScanRange ReadOnlyData{};  ///< `.rdata`: object locators, class hierarchies, vtables.
};

void* FindVirtualTableInRtti(const PeRtti& rtti, std::string_view className);

Result<BaseSubobject> FindBaseInRtti(const PeRtti& rtti, std::string_view className, std::string_view baseName);
#endif

}  // namespace VoltMod
