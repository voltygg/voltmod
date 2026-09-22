#include "Engine/Memory/VtableLookup.hpp"

#ifdef _WIN32

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace VoltMod
{

// MSVC x64 RTTI records store offsets; RVAs are relative to the module base.
static constexpr size_t TypeDescriptorName = 0x10;
static constexpr size_t LocatorOffset = 0x04;
static constexpr size_t LocatorTypeDescriptor = 0x0C;
static constexpr size_t LocatorHierarchy = 0x10;
static constexpr size_t LocatorSelf = 0x14;
static constexpr size_t LocatorSize = 0x18;
static constexpr size_t HierarchyBaseCount = 0x08;
static constexpr size_t HierarchyBaseList = 0x0C;
static constexpr size_t HierarchySize = 0x10;
static constexpr size_t BaseMemberOffset = 0x08;
static constexpr size_t BaseVirtualOffset = 0x0C;
static constexpr size_t BaseSize = 0x18;
static constexpr uint32_t MaxBases = 1024;

static const uint8_t* FindValue(const uint8_t* begin, const uint8_t* end, const void* needle, size_t len, size_t stride)
{
    if (!begin || !end || len == 0 || static_cast<size_t>(end - begin) < len)
    {
        return nullptr;
    }

    for (const uint8_t* at = begin; at + len <= end; at += stride)
    {
        if (std::memcmp(at, needle, len) == 0)
        {
            return at;
        }
    }
    return nullptr;
}

template <typename T>
static T ReadAt(const uint8_t* address)
{
    T value{};
    std::memcpy(&value, address, sizeof(T));
    return value;
}

static const uint8_t* End(const ScanRange& range)
{
    return range.Base ? range.Base + range.Size : nullptr;
}

static const uint8_t* AtRva(const PeRtti& rtti, int64_t rva, size_t bytes)
{
    if (rva < 0 || static_cast<uint64_t>(rva) > rtti.Size || rtti.Size - static_cast<size_t>(rva) < bytes)
    {
        return nullptr;
    }
    return rtti.Base + rva;
}

static std::array<std::string, 2> ClassAndStructNames(std::string_view name)
{
    return {std::format(".?AV{}@@", name), std::format(".?AU{}@@", name)};
}

static uint32_t FindTypeDescriptor(const PeRtti& rtti, std::string_view className)
{
    for (const std::string& mangled : ClassAndStructNames(className))
    {
        // Include the terminator to reject longer names.
        const uint8_t* name = FindValue(rtti.Data.Base, End(rtti.Data), mangled.c_str(), mangled.size() + 1, 1);
        if (name && static_cast<size_t>(name - rtti.Base) >= TypeDescriptorName)
        {
            return static_cast<uint32_t>(name - rtti.Base - TypeDescriptorName);
        }
    }
    return 0;
}

static bool TypeDescriptorMatches(const PeRtti& rtti, int32_t rva, std::span<const std::string> names)
{
    for (const std::string& mangled : names)
    {
        const uint8_t* name = AtRva(rtti, int64_t{rva} + TypeDescriptorName, mangled.size() + 1);
        if (name && std::memcmp(name, mangled.c_str(), mangled.size() + 1) == 0)
        {
            return true;
        }
    }
    return false;
}

static const uint8_t* FindLocator(const PeRtti& rtti, uint32_t typeDescriptor, uint32_t offset)
{
    const uint8_t* begin = rtti.ReadOnlyData.Base;
    const uint8_t* end = End(rtti.ReadOnlyData);
    for (const uint8_t* ref = FindValue(begin, end, &typeDescriptor, sizeof(uint32_t), 4); ref;
         ref = FindValue(ref + 4, end, &typeDescriptor, sizeof(uint32_t), 4))
    {
        if (static_cast<size_t>(ref - begin) < LocatorTypeDescriptor ||
            static_cast<size_t>(end - ref) < LocatorSize - LocatorTypeDescriptor)
        {
            continue;
        }

        const uint8_t* locator = ref - LocatorTypeDescriptor;
        if (ReadAt<uint32_t>(locator) == 1 && ReadAt<uint32_t>(locator + LocatorOffset) == offset &&
            ReadAt<uint32_t>(locator + LocatorSelf) == static_cast<uint32_t>(locator - rtti.Base))
        {
            return locator;
        }
    }
    return nullptr;
}

static void* TableAfter(const PeRtti& rtti, const uint8_t* locator)
{
    const uint8_t* word =
        FindValue(rtti.ReadOnlyData.Base, End(rtti.ReadOnlyData), &locator, sizeof(void*), sizeof(void*));
    return word ? const_cast<uint8_t*>(word + sizeof(void*)) : nullptr;
}

static std::pair<uint32_t, const uint8_t*> PrimaryLocator(const PeRtti& rtti, std::string_view className)
{
    const uint32_t typeDescriptor = FindTypeDescriptor(rtti, className);
    return {typeDescriptor, typeDescriptor ? FindLocator(rtti, typeDescriptor, 0) : nullptr};
}

void* FindVirtualTableInRtti(const PeRtti& rtti, std::string_view className)
{
    const uint8_t* locator = PrimaryLocator(rtti, className).second;
    return locator ? TableAfter(rtti, locator) : nullptr;
}

Result<BaseSubobject> FindBaseInRtti(const PeRtti& rtti, std::string_view className, std::string_view baseName)
{
    const auto [typeDescriptor, locator] = PrimaryLocator(rtti, className);
    if (!locator)
    {
        return std::unexpected(Error::NotFound(std::format("no RTTI for '{}'", className)));
    }

    const uint8_t* hierarchy = AtRva(rtti, ReadAt<int32_t>(locator + LocatorHierarchy), HierarchySize);
    const uint32_t count = hierarchy ? ReadAt<uint32_t>(hierarchy + HierarchyBaseCount) : 0;
    const uint8_t* bases = hierarchy && count <= MaxBases
                               ? AtRva(rtti, ReadAt<int32_t>(hierarchy + HierarchyBaseList), count * sizeof(int32_t))
                               : nullptr;
    if (!bases)
    {
        return std::unexpected(Error::Invalid(std::format("the RTTI base list of '{}' is unreadable", className)));
    }

    const std::array<std::string, 2> wanted = ClassAndStructNames(baseName);
    std::vector<int32_t> offsets;
    size_t virtualBases = 0;
    // The first entry is the class itself; remaining entries are bases and offsets.
    for (uint32_t i = 1; i < count; ++i)
    {
        const uint8_t* base = AtRva(rtti, ReadAt<int32_t>(bases + i * sizeof(int32_t)), BaseSize);
        if (!base || !TypeDescriptorMatches(rtti, ReadAt<int32_t>(base), wanted))
        {
            continue;
        }

        if (ReadAt<int32_t>(base + BaseVirtualOffset) != -1)
        {
            ++virtualBases;
        }
        else
        {
            offsets.push_back(ReadAt<int32_t>(base + BaseMemberOffset));
        }
    }

    if (offsets.size() + virtualBases > 1)
    {
        return std::unexpected(
            Error::Invalid(std::format("'{}' is a base {} times", baseName, offsets.size() + virtualBases)));
    }
    if (virtualBases)
    {
        return std::unexpected(Error::Unsupported(std::format("'{}' is a virtual base", baseName)));
    }
    if (offsets.empty())
    {
        return std::unexpected(Error::NotFound(std::format("'{}' is not a base", baseName)));
    }

    const int32_t offset = offsets.front();
    const uint8_t* baseLocator =
        offset == 0 ? locator : FindLocator(rtti, typeDescriptor, static_cast<uint32_t>(offset));
    return BaseSubobject{.Offset = offset, .Table = baseLocator ? TableAfter(rtti, baseLocator) : nullptr};
}

static ScanRange FindSection(const LoadedModule& module, std::string_view name)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.Base);
    if (module.Size < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE)
    {
        return {};
    }

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(module.Base + dos->e_lfanew);
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > module.Size ||
        nt->Signature != IMAGE_NT_SIGNATURE)
    {
        return {};
    }

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        // Section names are eight bytes, padded with NULs.
        std::string_view actual(reinterpret_cast<const char*>(sections[i].Name), IMAGE_SIZEOF_SHORT_NAME);
        if (const size_t padding = actual.find('\0'); padding != std::string_view::npos)
        {
            actual = actual.substr(0, padding);
        }
        if (actual != name)
        {
            continue;
        }

        // Use virtual size for the mapped image; raw size covers a zero virtual size.
        const DWORD size = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
        if (sections[i].VirtualAddress + static_cast<size_t>(size) > module.Size)
        {
            return {};
        }

        return {module.Base + sections[i].VirtualAddress, size};
    }
    return {};
}

static PeRtti RttiOf(const LoadedModule& module)
{
    return {.Base = module.Base,
            .Size = module.Size,
            .Data = FindSection(module, ".data"),
            .ReadOnlyData = FindSection(module, ".rdata")};
}

void* FindVirtualTableIn(const LoadedModule& module, std::string_view className)
{
    const PeRtti rtti = RttiOf(module);
    if (!rtti.Data.Base || !rtti.ReadOnlyData.Base)
    {
        return nullptr;
    }
    return FindVirtualTableInRtti(rtti, className);
}

Result<BaseSubobject> FindBaseIn(const LoadedModule& module, std::string_view className, std::string_view baseName)
{
    const PeRtti rtti = RttiOf(module);
    if (!rtti.Data.Base || !rtti.ReadOnlyData.Base)
    {
        return std::unexpected(Error::NotFound("the module has no RTTI sections"));
    }
    return FindBaseInRtti(rtti, className, baseName);
}

}  // namespace VoltMod

#endif
