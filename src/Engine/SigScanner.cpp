#include "Engine/SigScanner.hpp"

#include "Engine/BytePattern.hpp"

#include <VoltMod/Core/Log.hpp>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <psapi.h>
#else
#include <dlfcn.h>
#include <link.h>
#endif

namespace VoltMod
{

// A mapped region to scan: the whole image on Windows, one PT_LOAD segment on Linux (so we never
// read across an unmapped `-z separate-code` gap).
struct ScanRange
{
    const uint8_t* Base;
    size_t Size;
};

#ifdef _WIN32

/** The module's image and the ranges to scan, from one enumeration of the process. */
static bool FindImageAndRanges(const char* fileName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    HANDLE process = GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed = 0;

    if (!EnumProcessModules(process, modules, sizeof(modules), &needed))
        return false;

    ModuleImage best;
    for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i)
    {
        char path[MAX_PATH];
        if (!GetModuleFileNameA(modules[i], path, sizeof(path)))
            continue;

        const char* baseName = strrchr(path, '\\');
        if (!baseName)
            baseName = strrchr(path, '/');
        baseName = baseName ? baseName + 1 : path;

        if (_stricmp(baseName, fileName) != 0)
            continue;

        MODULEINFO info{};
        if (GetModuleInformation(process, modules[i], &info, sizeof(info)) && info.SizeOfImage > best.Size)
            best = {static_cast<const uint8_t*>(info.lpBaseOfDll), info.SizeOfImage, path};
    }

    if (!best.Base)
        return false;

    image = std::move(best);
    ranges.assign(1, ScanRange{image.Base, image.Size});
    return true;
}

#else

static const char* BaseName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/** The best match so far while walking the loaded objects; see @ref DlIterateCallback. */
struct ModuleScan
{
    const char* Name = nullptr;     // basename to match, e.g. "libserver.so"
    size_t BestSpan = 0;            // largest module span seen so far (selects the real lib)
    std::vector<ScanRange> Ranges;  // PT_LOAD segments of the selected module
    ModuleImage Image;              // load bias, span and on-disk path of the selected module
};

// Multiple objects can share the basename "libserver.so" (a loader stub plus the real game
// library), and a substring + first-match scan picks the stub. Match the exact basename and keep
// the largest-span mapping.
static int DlIterateCallback(struct dl_phdr_info* info, size_t /*size*/, void* data)
{
    auto* scan = static_cast<ModuleScan*>(data);
    if (!info->dlpi_name || strcmp(BaseName(info->dlpi_name), scan->Name) != 0)
        return 0;

    size_t span = 0;
    std::vector<ScanRange> segments;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
            continue;

        span = std::max(span, static_cast<size_t>(phdr.p_vaddr + phdr.p_memsz));
        segments.push_back({reinterpret_cast<const uint8_t*>(info->dlpi_addr + phdr.p_vaddr), phdr.p_memsz});
    }

    if (span > scan->BestSpan)
    {
        scan->BestSpan = span;
        scan->Ranges = std::move(segments);
        // l_addr, not the first segment's mapped address: ELF symbol values are link-time
        // addresses that must be biased by exactly this to become runtime addresses.
        scan->Image = {reinterpret_cast<const uint8_t*>(info->dlpi_addr), span, info->dlpi_name};
    }
    return 0;  // keep iterating; the largest match wins
}

/** The module's image and the ranges to scan, from one walk of the loaded objects. */
static bool FindImageAndRanges(const char* fileName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    ModuleScan scan{.Name = fileName};
    dl_iterate_phdr(DlIterateCallback, &scan);
    if (scan.Ranges.empty())
        return false;

    image = std::move(scan.Image);
    ranges = std::move(scan.Ranges);
    return true;
}

#endif

/**
 * Byte frequencies across @p ranges, counted once per module.
 *
 * Process-wide rather than per-load, like the schema field cache: a mapped module's bytes do not
 * change while it is mapped, and every signature scanned against it wants the same answer. Keyed
 * by image base, which is what tells two mapped modules apart. Counted over the ranges rather than
 * the image span because on Linux that span covers unmapped gaps between PT_LOAD segments.
 */
static const ByteHistogram& FrequenciesOf(const ModuleImage& image, const std::vector<ScanRange>& ranges)
{
    static std::map<const uint8_t*, ByteHistogram> cache;

    const auto found = cache.find(image.Base);
    if (found != cache.end())
        return found->second;

    ByteHistogram counts{};
    for (const auto& range : ranges)
        CountBytes(range.Base, range.Size, counts);

    return cache.emplace(image.Base, counts).first->second;
}

std::string PlatformModuleName(const char* moduleName)
{
#ifdef _WIN32
    return std::string(moduleName) + ".dll";
#else
    return std::string("lib") + moduleName + ".so";
#endif
}

bool FindModuleImage(const char* moduleName, ModuleImage& image)
{
    std::vector<ScanRange> ranges;  // unused; the image is what the caller asked for
    return FindImageAndRanges(PlatformModuleName(moduleName).c_str(), image, ranges);
}

ScanResult FindPatternEx(const char* moduleName, const std::string& pattern)
{
    const std::string fullName = PlatformModuleName(moduleName);

    ModuleImage image;
    std::vector<ScanRange> ranges;
    if (!FindImageAndRanges(fullName.c_str(), image, ranges))
    {
        Log::Error("SigScanner: Module '{}' not found.", fullName);
        return {};
    }

    const std::vector<PatternByte> bytes = ParsePattern(pattern);
    const size_t anchor = AnchorOf(bytes, FrequenciesOf(image, ranges));

    const uint8_t* first = nullptr;
    for (const auto& range : ranges)
    {
        // Keep going after a hit rather than returning it: a pattern matching twice is ambiguous,
        // and taking the first match would silently bind to whichever one the linker put first.
        for (size_t at = 0; at < range.Size;)
        {
            const uint8_t* hit = FindFirst(range.Base + at, range.Size - at, bytes, anchor);
            if (!hit)
                break;

            if (first)
            {
                Log::Warn("SigScanner: Pattern ambiguous in '{}' (2+ matches); refusing it.", fullName);
                return {const_cast<uint8_t*>(first), false, std::move(image)};
            }
            first = hit;
            at = static_cast<size_t>(hit - range.Base) + 1;
        }
    }

    if (!first)
        Log::Warn("SigScanner: Pattern not found in '{}'.", fullName);
    return {const_cast<uint8_t*>(first), true, std::move(image)};
}

void* FindPattern(const char* moduleName, const std::string& pattern)
{
    return FindPatternEx(moduleName, pattern).Address;
}

uintptr_t ResolveRelativeAddress(const ModuleImage& image, uintptr_t matchAddress, int ripOffset, int ripSize)
{
    if (matchAddress == 0 || !image.Base)
        return 0;

    // The displacement itself must be inside the mapping: a pattern that matched near the end of
    // the module, or a rel32At past the instruction, would otherwise read unmapped memory.
    if (!Rel32ReadInBounds(reinterpret_cast<uintptr_t>(image.Base), image.Size, matchAddress, ripOffset))
        return 0;

    const uintptr_t site = Rel32Site(matchAddress, ripOffset);
    int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(site), sizeof(displacement));
    return Rel32Target(site, displacement, ripSize);
}

bool IsExecutableAddress(const void* address)
{
    if (!address)
        return false;

#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT)
        return false;

    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return (info.Protect & executable) != 0 && (info.Protect & PAGE_GUARD) == 0;
#else
    const auto target = reinterpret_cast<unsigned long>(address);
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line))
    {
        unsigned long start = 0;
        unsigned long end = 0;
        char perms[5] = {};
        if (std::sscanf(line.c_str(), "%lx-%lx %4s", &start, &end, perms) != 3)
            continue;
        if (target >= start && target < end)
            return perms[2] == 'x';
    }
    return false;
#endif
}

}  // namespace VoltMod
