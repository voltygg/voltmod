#include "Engine/VtableLookup.hpp"

#ifdef _WIN32

// The section walk below uses the IMAGE_* declarations.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace VoltMod
{

/** First `needle` in [begin, end), stepping `stride` bytes to preserve alignment. */
static const uint8_t* FindValue(const uint8_t* begin, const uint8_t* end, const void* needle, size_t len, size_t stride)
{
    if (!begin || !end || len == 0 || static_cast<size_t>(end - begin) < len)
        return nullptr;

    for (const uint8_t* at = begin; at + len <= end; at += stride)
    {
        if (std::memcmp(at, needle, len) == 0)
            return at;
    }
    return nullptr;
}

struct Section
{
    const uint8_t* Begin = nullptr;
    const uint8_t* End = nullptr;
};

static Section FindSection(const ModuleImage& image, const char* name)
{
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image.Base);
    if (image.Size < sizeof(IMAGE_DOS_HEADER) || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return {};

    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image.Base + dos->e_lfanew);
    if (static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) > image.Size ||
        nt->Signature != IMAGE_NT_SIGNATURE)
        return {};

    const IMAGE_SECTION_HEADER* sections = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i)
    {
        // Section names are eight bytes and may not be NUL-terminated.
        if (std::strncmp(reinterpret_cast<const char*>(sections[i].Name), name, IMAGE_SIZEOF_SHORT_NAME) != 0)
            continue;

        // This is a mapped image, so use virtual size; raw size covers a zero virtual size.
        const DWORD size = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
        if (sections[i].VirtualAddress + static_cast<size_t>(size) > image.Size)
            return {};

        const uint8_t* begin = image.Base + sections[i].VirtualAddress;
        return {begin, begin + size};
    }
    return {};
}

template <typename T>
static T ReadAt(const uint8_t* address)
{
    T value{};
    std::memcpy(&value, address, sizeof(T));
    return value;
}

void* FindVirtualTableIn(const ModuleImage& image, const char* className)
{
    const Section data = FindSection(image, ".data");
    const Section rdata = FindSection(image, ".rdata");
    if (!data.Begin || !rdata.Begin)
        return nullptr;

    // RTTITypeDescriptor stores the name at 0x10; include the terminator for exact matching.
    const std::string mangled = ".?AV" + std::string(className) + "@@";
    const uint8_t* mangledName = FindValue(data.Begin, data.End, mangled.c_str(), mangled.size() + 1, 1);
    if (!mangledName || mangledName - image.Base < 0x10)
        return nullptr;

    const uint8_t* typeDescriptor = mangledName - 0x10;
    const auto typeDescriptorRva = static_cast<uint32_t>(typeDescriptor - image.Base);

    // RTTICompleteObjectLocator stores pTypeDescriptor at offset 0xC.
    constexpr ptrdiff_t PTypeDescriptorOffset = 0xC;

    for (const uint8_t* ref = FindValue(rdata.Begin, rdata.End, &typeDescriptorRva, sizeof(uint32_t), 4); ref;
         ref = FindValue(ref + 4, rdata.End, &typeDescriptorRva, sizeof(uint32_t), 4))
    {
        if (ref - PTypeDescriptorOffset < rdata.Begin)
            continue;

        // Require an x64 complete-object locator, not a coincidental matching RVA.
        const uint8_t* locator = ref - PTypeDescriptorOffset;
        if (ReadAt<uint32_t>(locator) != 1 || ReadAt<uint32_t>(locator + 4) != 0)
            continue;

        // The locator pointer immediately precedes the first virtual function.
        if (const uint8_t* slot = FindValue(rdata.Begin, rdata.End, &locator, sizeof(void*), sizeof(void*)))
            return const_cast<uint8_t*>(slot + sizeof(void*));
    }
    return nullptr;
}

}  // namespace VoltMod

#endif
