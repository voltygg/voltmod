#include "Engine/Memory/VtableLookup.hpp"

#include "Engine/Memory/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

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

static uintptr_t ReadWord(uintptr_t address)
{
    uintptr_t word = 0;
    std::memcpy(&word, reinterpret_cast<const void*>(address), sizeof(word));
    return word;
}

/** Every aligned word in @p ranges holding @p value, with a readable word on each side. */
static std::vector<uintptr_t> FindWords(std::span<const ScanRange> ranges, uintptr_t value)
{
    std::vector<uintptr_t> found;
    for (const ScanRange& range : ranges)
    {
        const auto begin = reinterpret_cast<uintptr_t>(range.Base);
        const uintptr_t first = ((begin + sizeof(void*) - 1) & ~uintptr_t{sizeof(void*) - 1}) + sizeof(void*);
        for (uintptr_t at = first; at + 2 * sizeof(void*) <= begin + range.Size; at += sizeof(void*))
        {
            if (ReadWord(at) == value)
                found.push_back(at);
        }
    }
    return found;
}

void* FindVirtualTableByTypeName(std::span<const ScanRange> ranges, const char* className)
{
    // The type name is "<length><name>\0"; a typeinfo is {vptr, name}; a vtable is {offset-to-top, typeinfo, slots}.
    const std::string typeName = std::to_string(std::strlen(className)) + className;
    const std::string_view needle(typeName.c_str(), typeName.size() + 1);
    for (const ScanRange& range : ranges)
    {
        const std::string_view memory(reinterpret_cast<const char*>(range.Base), range.Size);
        for (size_t at = memory.find(needle); at != std::string_view::npos; at = memory.find(needle, at + 1))
        {
            // Preceded by a name character, the match is the tail of a longer mangled name.
            if (at > 0 && (std::isalnum(static_cast<unsigned char>(memory[at - 1])) || memory[at - 1] == '_'))
                continue;

            for (uintptr_t name : FindWords(ranges, reinterpret_cast<uintptr_t>(range.Base) + at))
            {
                for (uintptr_t typeInfo : FindWords(ranges, name - sizeof(void*)))
                {
                    // The primary table has a zero offset-to-top and code in its first slot.
                    if (ReadWord(typeInfo - sizeof(void*)) == 0 &&
                        IsExecutableAddress(reinterpret_cast<const void*>(ReadWord(typeInfo + sizeof(void*)))))
                        return reinterpret_cast<void*>(typeInfo + sizeof(void*));
                }
            }
        }
    }
    return nullptr;
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
        if (slots[index] == function || (originalOf && originalOf(slots, index) == function))
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
