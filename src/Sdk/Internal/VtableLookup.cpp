#include "Sdk/Internal/VtableLookup.hpp"

#include "Sdk/Internal/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cstddef>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <elf.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace VoltMod::Sdk
{

using namespace VoltMod::Core;

namespace
{

#ifdef _WIN32

/**
 * First occurrence of `needle` in [begin, end), stepping `stride` bytes at a time. Structured data
 * (RVAs, pointers) is naturally aligned, so a stride matching its width both speeds the scan up and
 * rules out matches straddling two unrelated values.
 */
const uint8_t* FindValue(const uint8_t* begin, const uint8_t* end, const void* needle, size_t len, size_t stride)
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

Section FindSection(const ModuleImage& image, const char* name)
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
        // Section names are 8 bytes and only NUL-terminated when shorter.
        if (std::strncmp(reinterpret_cast<const char*>(sections[i].Name), name, IMAGE_SIZEOF_SHORT_NAME) != 0)
            continue;

        // The image is mapped, not the file on disk, so the section spans its virtual size.
        // SizeOfRawData is only the fallback for a header that leaves VirtualSize at 0.
        const DWORD size = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
        if (sections[i].VirtualAddress + static_cast<size_t>(size) > image.Size)
            return {};

        const uint8_t* begin = image.Base + sections[i].VirtualAddress;
        return {begin, begin + size};
    }
    return {};
}

template <typename T>
T ReadAt(const uint8_t* address)
{
    T value{};
    std::memcpy(&value, address, sizeof(T));
    return value;
}

void* FindVirtualTableWin(const ModuleImage& image, const char* className)
{
    const Section data = FindSection(image, ".data");
    const Section rdata = FindSection(image, ".rdata");
    if (!data.Begin || !rdata.Begin)
        return nullptr;

    // MSVC emits one RTTITypeDescriptor per polymorphic class into .data, laid out as
    // { type_info vftable, spare, char name[] } - so the mangled name starts 0x10 bytes in.
    // Matching the terminator too keeps ".?AVCFoo@@" from matching ".?AVCFooBar@@".
    const std::string mangled = ".?AV" + std::string(className) + "@@";
    const uint8_t* mangledName = FindValue(data.Begin, data.End, mangled.c_str(), mangled.size() + 1, 1);
    if (!mangledName || mangledName - image.Base < 0x10)
        return nullptr;

    const uint8_t* typeDescriptor = mangledName - 0x10;
    const auto typeDescriptorRva = static_cast<uint32_t>(typeDescriptor - image.Base);

    // RTTICompleteObjectLocator: { signature, offset, cdOffset, pTypeDescriptor(RVA), ... }.
    constexpr ptrdiff_t PTypeDescriptorOffset = 0xC;

    for (const uint8_t* ref = FindValue(rdata.Begin, rdata.End, &typeDescriptorRva, sizeof(uint32_t), 4); ref;
         ref = FindValue(ref + 4, rdata.End, &typeDescriptorRva, sizeof(uint32_t), 4))
    {
        if (ref - PTypeDescriptorOffset < rdata.Begin)
            continue;

        // Accept the reference only when it reads as a complete-object (offset 0) x64 locator
        // (signature 1), not an unrelated word that happens to equal the RVA.
        const uint8_t* locator = ref - PTypeDescriptorOffset;
        if (ReadAt<uint32_t>(locator) != 1 || ReadAt<uint32_t>(locator + 4) != 0)
            continue;

        // A vtable stores its locator pointer immediately before the first virtual function.
        if (const uint8_t* slot = FindValue(rdata.Begin, rdata.End, &locator, sizeof(void*), sizeof(void*)))
            return const_cast<uint8_t*>(slot + sizeof(void*));
    }
    return nullptr;
}

#else

/** Read-only mmap of a file for the duration of the lookup. */
class MappedFile
{
public:
    explicit MappedFile(const std::string& path)
    {
        const int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1)
            return;

        struct stat info{};
        if (fstat(fd, &info) == 0 && info.st_size > 0)
        {
            void* map = mmap(nullptr, static_cast<size_t>(info.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
            if (map != MAP_FAILED)
            {
                _base = static_cast<const uint8_t*>(map);
                _size = static_cast<size_t>(info.st_size);
            }
        }
        close(fd);
    }

    ~MappedFile()
    {
        if (_base)
            munmap(const_cast<uint8_t*>(_base), _size);
    }

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    explicit operator bool() const { return _base != nullptr; }
    const uint8_t* Base() const { return _base; }
    size_t Size() const { return _size; }

    /** Typed view of `count` records at `offset`, or nullptr when they fall outside the file. */
    template <typename T>
    const T* At(size_t offset, size_t count = 1) const
    {
        if (!_base || count == 0 || offset > _size || (_size - offset) / sizeof(T) < count)
            return nullptr;
        return reinterpret_cast<const T*>(_base + offset);
    }

private:
    const uint8_t* _base = nullptr;
    size_t _size = 0;
};

/** Link-time address of `symbol` in one of the file's symbol tables, or 0 when absent. */
uint64_t FindSymbolValue(const MappedFile& elf, const std::string& symbol)
{
    const auto* header = elf.At<Elf64_Ehdr>(0);
    if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 || header->e_ident[EI_CLASS] != ELFCLASS64)
        return 0;

    const auto* sections = elf.At<Elf64_Shdr>(header->e_shoff, header->e_shnum);
    if (!sections || header->e_shentsize != sizeof(Elf64_Shdr))
        return 0;

    // .symtab first: it is complete when present, while .dynsym only carries exported symbols. A
    // fully stripped library has neither and the caller degrades.
    for (const Elf64_Word wanted : {Elf64_Word{SHT_SYMTAB}, Elf64_Word{SHT_DYNSYM}})
    {
        for (uint16_t i = 0; i < header->e_shnum; ++i)
        {
            const Elf64_Shdr& section = sections[i];
            if (section.sh_type != wanted || section.sh_entsize != sizeof(Elf64_Sym) ||
                section.sh_link >= header->e_shnum)
                continue;

            const Elf64_Shdr& strings = sections[section.sh_link];
            const auto* names = elf.At<char>(strings.sh_offset, strings.sh_size);
            const auto* symbols = elf.At<Elf64_Sym>(section.sh_offset, section.sh_size / sizeof(Elf64_Sym));
            if (!names || !symbols)
                continue;

            for (size_t s = 0; s < section.sh_size / sizeof(Elf64_Sym); ++s)
            {
                if (symbols[s].st_name >= strings.sh_size || symbols[s].st_value == 0)
                    continue;
                if (symbol == names + symbols[s].st_name)
                    return symbols[s].st_value;
            }
        }
    }
    return 0;
}

void* FindVirtualTableElf(const ModuleImage& image, const char* className)
{
    if (image.Path.empty())
        return nullptr;

    MappedFile elf(image.Path);
    if (!elf)
        return nullptr;

    // Itanium ABI: the vtable symbol is _ZTV<length><name>.
    const std::string symbol = "_ZTV" + std::to_string(std::strlen(className)) + className;
    const uint64_t value = FindSymbolValue(elf, symbol);
    if (value == 0)
        return nullptr;

    // The symbol addresses the whole vtable. Objects carry a pointer two words in, past the
    // offset-to-top and typeinfo slots.
    return const_cast<uint8_t*>(image.Base + value + 2 * sizeof(void*));
}

#endif

}  // namespace

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

#ifdef _WIN32
    void* vtable = FindVirtualTableWin(image, className);
#else
    void* vtable = FindVirtualTableElf(image, className);
#endif

    if (!vtable)
        Log::Warn("VtableLookup: '{}' vtable not found in '{}'.", className, PlatformModuleName(moduleName));
    return vtable;
}

}  // namespace VoltMod::Sdk
