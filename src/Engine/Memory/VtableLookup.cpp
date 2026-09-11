#include "Engine/Memory/VtableLookup.hpp"

#include "Engine/Memory/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cstdint>
#include <cstring>

namespace VoltMod
{

void* FindVirtualTable(const char* moduleName, const char* className)
{
    if (!moduleName || !className || !*className)
        return nullptr;

    ModuleImage image;
    if (!FindModuleImage(moduleName, image))
    {
        Log::Warn("VtableLookup: Module '{}' not found.", PlatformModuleName(moduleName));
        return nullptr;
    }

    void* vtable = FindVirtualTableIn(image, className);
    if (!vtable)
        Log::Warn("VtableLookup: '{}' vtable not found in '{}'.", className, PlatformModuleName(moduleName));
    return vtable;
}

/** How many object words the blind walk tries as vptrs. */
static constexpr int MaxBases = 8;

std::optional<int> FindSlotInTable(void* table, const void* function, const OriginalVfn& originalOf, int maxSlots)
{
    if (!table || !function)
        return std::nullopt;

    auto** slots = static_cast<void**>(table);
    for (int index = 0; index < maxSlots; ++index)
    {
        // Stop when the table ends or the current slot is not executable.
        if (!IsReadableAddress(&slots[index], sizeof(void*)) || !IsExecutableAddress(slots[index]))
            break;
        // Match either the installed hook or the original function it replaced.
        if (slots[index] == function || (originalOf && originalOf(&slots[index]) == function))
            return index;
    }
    return std::nullopt;
}

std::optional<VTableSlot> FindVTableSlot(const void* instance, const void* function, const OriginalVfn& originalOf)
{
    if (!instance || !function)
        return std::nullopt;

    const auto* const object = static_cast<const uint8_t*>(instance);
    for (int table = 0; table < MaxBases; ++table)
    {
        const int baseOffset = table * static_cast<int>(sizeof(void*));

        // Validate the object memory before reading each possible vptr.
        if (!IsReadableAddress(object + baseOffset, sizeof(void*)))
            break;

        void** candidate = nullptr;
        std::memcpy(&candidate, object + baseOffset, sizeof(candidate));

        // A candidate may be any data member, so validate its pointer and first executable slot.
        if (!candidate || !IsReadableAddress(candidate, sizeof(void*)) || !IsExecutableAddress(candidate[0]))
            continue;

        if (auto index = FindSlotInTable(static_cast<void*>(candidate), function, originalOf))
            return VTableSlot{.Table = static_cast<void*>(candidate), .Index = *index, .BaseOffset = baseOffset};
    }
    return std::nullopt;
}

}  // namespace VoltMod
