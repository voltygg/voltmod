#include "Engine/Memory/VtableLookup.hpp"

#include "Engine/Memory/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

void* FindVirtualTable(std::string_view moduleName, std::string_view className)
{
    if (moduleName.empty() || className.empty())
    {
        return nullptr;
    }

    Image module;
    if (!FindImage(moduleName, module))
    {
        Log::Warn("VtableLookup: Module '{}' not found.", PlatformModuleName(moduleName));
        return nullptr;
    }

    void* vtable = FindVirtualTableIn(module, className);
    if (!vtable)
    {
        Log::Warn("VtableLookup: '{}' vtable not found in '{}'.", className, PlatformModuleName(moduleName));
    }
    return vtable;
}

static uintptr_t ReadWord(uintptr_t address)
{
    uintptr_t word = 0;
    std::memcpy(&word, reinterpret_cast<const void*>(address), sizeof(word));
    return word;
}

static uint32_t ReadU32(uintptr_t address)
{
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}

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
            {
                found.push_back(at);
            }
        }
    }
    return found;
}

std::string LengthPrefixedName(std::string_view className)
{
    return std::to_string(className.size()) + std::string(className);
}

void* FindVirtualTableByTypeName(std::span<const ScanRange> ranges, std::string_view className)
{
    // Itanium names are length-prefixed; typeinfos hold a name and vtables hold offset-to-top and typeinfo.
    const std::string typeName = LengthPrefixedName(className);
    const std::string_view needle(typeName.c_str(), typeName.size() + 1);
    for (const ScanRange& range : ranges)
    {
        const std::string_view memory(reinterpret_cast<const char*>(range.Base), range.Size);
        for (size_t at = memory.find(needle); at != std::string_view::npos; at = memory.find(needle, at + 1))
        {
            // A preceding name character means this is part of a longer mangled name.
            if (at > 0 && (std::isalnum(static_cast<unsigned char>(memory[at - 1])) || memory[at - 1] == '_'))
            {
                continue;
            }

            for (uintptr_t name : FindWords(ranges, reinterpret_cast<uintptr_t>(range.Base) + at))
            {
                for (uintptr_t typeInfo : FindWords(ranges, name - sizeof(void*)))
                {
                    // The primary table has offset-to-top zero and executable code in its first slot.
                    if (ReadWord(typeInfo - sizeof(void*)) == 0 &&
                        IsExecutableAddress(reinterpret_cast<const void*>(ReadWord(typeInfo + sizeof(void*)))))
                    {
                        return reinterpret_cast<void*>(typeInfo + sizeof(void*));
                    }
                }
            }
        }
    }
    return nullptr;
}

bool IsInstanceOf(const void* object, const void* table)
{
    return table && IsReadableAddress(object, sizeof(void*)) &&
           ReadWord(reinterpret_cast<uintptr_t>(object)) == reinterpret_cast<uintptr_t>(table);
}

// Itanium typeinfo layouts: {vptr, name}, optionally followed by base or base-list records.
static constexpr size_t TypeInfoBase = 2 * sizeof(void*);
static constexpr size_t TypeInfoFlags = 2 * sizeof(void*);
static constexpr size_t TypeInfoCount = TypeInfoFlags + sizeof(uint32_t);
static constexpr size_t TypeInfoBaseList = 3 * sizeof(void*);
static constexpr size_t BaseEntrySize = 2 * sizeof(void*);
static constexpr uintptr_t VirtualBaseFlag = 1;

static constexpr uint32_t MaxBases = 64;
static constexpr int MaxDepth = 32;

static bool LooksLikeTypeInfo(uintptr_t address)
{
    if (!address || address % alignof(void*) != 0 ||
        !IsReadableAddress(reinterpret_cast<const void*>(address), 2 * sizeof(void*)))
    {
        return false;
    }

    const auto* name = reinterpret_cast<const char*>(ReadWord(address + sizeof(void*)));
    if (!IsReadableAddress(name, 1))
    {
        return false;
    }
    return std::isdigit(static_cast<unsigned char>(*name)) || *name == 'N' || *name == '*';
}

static bool NameIs(uintptr_t address, std::string_view wanted)
{
    const auto* name = reinterpret_cast<const char*>(ReadWord(address + sizeof(void*)));
    return IsReadableAddress(name, wanted.size() + 1) && std::string_view(name, wanted.size()) == wanted &&
           name[wanted.size()] == '\0';
}

static bool HasSingleBase(uintptr_t typeInfo, const TypeInfoKinds& kinds)
{
    if (kinds.SingleBase)
    {
        return ReadWord(typeInfo) == kinds.SingleBase;
    }
    return IsReadableAddress(reinterpret_cast<const void*>(typeInfo), TypeInfoBase + sizeof(void*)) &&
           LooksLikeTypeInfo(ReadWord(typeInfo + TypeInfoBase));
}

static uint32_t BaseCount(uintptr_t typeInfo, const TypeInfoKinds& kinds)
{
    if (kinds.MultipleBases && ReadWord(typeInfo) != kinds.MultipleBases)
    {
        return 0;
    }
    if (!IsReadableAddress(reinterpret_cast<const void*>(typeInfo), TypeInfoBaseList))
    {
        return 0;
    }

    const uint32_t count = ReadU32(typeInfo + TypeInfoCount);
    if (count == 0 || count > MaxBases ||
        !IsReadableAddress(reinterpret_cast<const void*>(typeInfo + TypeInfoBaseList), count * BaseEntrySize))
    {
        return 0;
    }

    // Without the real vptr, validate the flags and first-base shape.
    if (!kinds.MultipleBases &&
        (ReadU32(typeInfo + TypeInfoFlags) > 3 || !LooksLikeTypeInfo(ReadWord(typeInfo + TypeInfoBaseList))))
    {
        return 0;
    }
    return count;
}

struct FoundBase
{
    intptr_t Offset = 0;
    bool Virtual = false;
};

static void CollectBases(uintptr_t typeInfo, std::string_view wanted, FoundBase at, const TypeInfoKinds& kinds,
                         int depth, std::vector<FoundBase>& found)
{
    if (depth > MaxDepth)
    {
        return;
    }

    const auto visit = [&](uintptr_t base, intptr_t offset, bool isVirtual) {
        if (!LooksLikeTypeInfo(base))
        {
            return;
        }

        const FoundBase here{.Offset = at.Offset + offset, .Virtual = at.Virtual || isVirtual};
        if (NameIs(base, wanted))
        {
            found.push_back(here);
        }
        else
        {
            CollectBases(base, wanted, here, kinds, depth + 1, found);
        }
    };

    if (HasSingleBase(typeInfo, kinds))
    {
        visit(ReadWord(typeInfo + TypeInfoBase), 0, false);
        return;
    }

    const uint32_t count = BaseCount(typeInfo, kinds);
    for (uint32_t i = 0; i < count; ++i)
    {
        const uintptr_t entry = typeInfo + TypeInfoBaseList + i * BaseEntrySize;
        const auto offsetFlags = static_cast<intptr_t>(ReadWord(entry + sizeof(void*)));
        visit(ReadWord(entry), offsetFlags >> 8, (static_cast<uintptr_t>(offsetFlags) & VirtualBaseFlag) != 0);
    }
}

Result<int> FindBaseOffsetByTypeInfo(const void* typeInfo, std::string_view baseName, const TypeInfoKinds& kinds)
{
    const auto address = reinterpret_cast<uintptr_t>(typeInfo);
    if (!LooksLikeTypeInfo(address))
    {
        return std::unexpected(Error::Invalid("the class has no readable typeinfo"));
    }

    const std::string wanted = LengthPrefixedName(baseName);
    std::vector<FoundBase> found;
    CollectBases(address, wanted, {}, kinds, 0, found);

    if (found.empty())
    {
        return std::unexpected(Error::NotFound(std::format("'{}' is not a base", baseName)));
    }
    if (found.size() > 1)
    {
        return std::unexpected(Error::Invalid(std::format("'{}' is a base {} times", baseName, found.size())));
    }
    if (found.front().Virtual)
    {
        return std::unexpected(Error::Unsupported(std::format("'{}' is a virtual base", baseName)));
    }
    return static_cast<int>(found.front().Offset);
}

void* FindVirtualTableByTypeInfo(std::span<const ScanRange> ranges, const void* typeInfo, intptr_t offsetToTop)
{
    void* found = nullptr;
    for (uintptr_t at : FindWords(ranges, reinterpret_cast<uintptr_t>(typeInfo)))
    {
        const uintptr_t table = at + sizeof(void*);
        if (static_cast<intptr_t>(ReadWord(at - sizeof(void*))) != offsetToTop ||
            !IsExecutableAddress(reinterpret_cast<const void*>(ReadWord(table))))
        {
            continue;
        }

        if (found)
        {
            return nullptr;
        }
        found = reinterpret_cast<void*>(table);
    }
    return found;
}

}  // namespace VoltMod
