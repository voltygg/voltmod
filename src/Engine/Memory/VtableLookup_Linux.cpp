#include "Engine/Memory/VtableLookup.hpp"

#ifndef _WIN32

#include <cstdint>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace VoltMod
{

/** Read-only file mapping scoped to one lookup. */
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

    /** Typed view of `count` records at `offset`, or nullptr if out of bounds. */
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

/** Link-time value of `symbol`, or 0 when absent from both symbol tables. */
static uint64_t FindSymbolValue(const MappedFile& elf, const std::string& symbol)
{
    const auto* header = elf.At<Elf64_Ehdr>(0);
    if (!header || std::memcmp(header->e_ident, ELFMAG, SELFMAG) != 0 || header->e_ident[EI_CLASS] != ELFCLASS64)
        return 0;

    const auto* sections = elf.At<Elf64_Shdr>(header->e_shoff, header->e_shnum);
    if (!sections || header->e_shentsize != sizeof(Elf64_Shdr))
        return 0;

    // .symtab is complete when present; .dynsym contains only exports.
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

void* FindVirtualTableIn(const ModuleImage& image, const char* className)
{
    if (image.Path.empty())
        return nullptr;

    MappedFile elf(image.Path);
    if (!elf)
        return nullptr;

    // Itanium ABI vtable symbols use _ZTV<length><name>.
    const std::string symbol = "_ZTV" + std::to_string(std::strlen(className)) + className;
    const uint64_t value = FindSymbolValue(elf, symbol);
    if (value == 0)
        return nullptr;

    // Object vptrs point past offset-to-top and typeinfo.
    return const_cast<uint8_t*>(image.Base + value + 2 * sizeof(void*));
}

}  // namespace VoltMod

#endif
