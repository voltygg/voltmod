#include "Engine/SigScanner.hpp"

#include <VoltMod/Core/Log.hpp>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
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

struct PatternByte
{
    uint8_t value;
    bool wildcard;
};

// A mapped region to scan: the whole image on Windows, one PT_LOAD segment on Linux (so we never
// read across an unmapped `-z separate-code` gap).
struct ScanRange
{
    const uint8_t* base;
    size_t size;
};

static std::vector<PatternByte> ParsePattern(const std::string& pattern)
{
    std::vector<PatternByte> bytes;
    std::istringstream stream(pattern);
    std::string token;

    while (stream >> token)
    {
        if (token == "?" || token == "??")
        {
            bytes.push_back({0, true});
        }
        else
        {
            // from_chars keeps a bad token local to this signature.
            unsigned value = 0;
            const char* end = token.data() + token.size();
            auto [ptr, ec] = std::from_chars(token.data(), end, value, 16);
            if (ec != std::errc{} || ptr != end || value > 0xFF)
            {
                Log::Error("Signature pattern has an invalid byte '{}'; ignoring the pattern.", token);
                return {};
            }
            bytes.push_back({static_cast<uint8_t>(value), false});
        }
    }
    return bytes;
}

/** Whether @p pattern matches the bytes at @p at, which must have room for all of it. */
static bool Matches(const uint8_t* at, const std::vector<PatternByte>& pattern)
{
    for (size_t j = 0; j < pattern.size(); ++j)
    {
        if (!pattern[j].wildcard && at[j] != pattern[j].value)
            return false;
    }
    return true;
}

/** How often each byte value occurs in a module, for choosing a scan anchor. */
using ByteHistogram = std::array<size_t, 256>;

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
    {
        for (size_t i = 0; i < range.size; ++i)
            ++counts[range.base[i]];
    }
    return cache.emplace(image.Base, counts).first->second;
}

/**
 * Index of the pattern byte to search for, or `pattern.size()` when it is all wildcards.
 *
 * Which byte is searched for is what decides the scan's cost, because the rest of the pattern is
 * only compared where that byte lands. The first byte - the obvious anchor - is close to the worst
 * one for x86-64: nearly every signature opens with a REX prefix (0x48), which saturates the
 * image. Measured over CS2's server.dll, anchoring on the rarest byte instead scans ~16x faster.
 */
static size_t AnchorOf(const std::vector<PatternByte>& pattern, const ByteHistogram& frequencies)
{
    size_t anchor = pattern.size();
    size_t rarest = SIZE_MAX;

    for (size_t i = 0; i < pattern.size(); ++i)
    {
        if (pattern[i].wildcard)
            continue;
        if (const size_t count = frequencies[pattern[i].value]; count < rarest)
        {
            rarest = count;
            anchor = i;
        }
    }
    return anchor;
}

/** First match of @p pattern in [base, base + size), searching by its @p anchor byte. */
static void* ScanMemory(const uint8_t* base, size_t size, const std::vector<PatternByte>& pattern, size_t anchor)
{
    if (pattern.empty() || size < pattern.size())
        return nullptr;

    const size_t scanEnd = size - pattern.size();

    // Nothing to anchor on: an all-wildcard pattern matches at the first offset.
    if (anchor >= pattern.size())
        return const_cast<uint8_t*>(base);

    const uint8_t wanted = pattern[anchor].value;
    size_t i = 0;
    while (i <= scanEnd)
    {
        // The anchor byte sits at i + anchor, and the last offset worth testing is scanEnd, so the
        // search window ends at scanEnd + anchor - everything memchr skips cannot match.
        const auto* hit = static_cast<const uint8_t*>(std::memchr(base + i + anchor, wanted, scanEnd - i + 1));
        if (!hit)
            return nullptr;

        i = static_cast<size_t>(hit - base) - anchor;
        if (Matches(base + i, pattern))
            return const_cast<uint8_t*>(base + i);
        ++i;
    }
    return nullptr;
}

#ifdef _WIN32

static bool FindImage(const char* fileName, ModuleImage& image)
{
    HANDLE hProcess = GetCurrentProcess();
    HMODULE hModules[1024];
    DWORD cbNeeded = 0;

    if (!EnumProcessModules(hProcess, hModules, sizeof(hModules), &cbNeeded))
        return false;

    DWORD moduleCount = cbNeeded / sizeof(HMODULE);
    ModuleImage best;

    for (DWORD i = 0; i < moduleCount; ++i)
    {
        char modPath[MAX_PATH];
        if (!GetModuleFileNameA(hModules[i], modPath, sizeof(modPath)))
            continue;

        const char* baseName = strrchr(modPath, '\\');
        if (!baseName)
            baseName = strrchr(modPath, '/');
        baseName = baseName ? baseName + 1 : modPath;

        if (_stricmp(baseName, fileName) != 0)
            continue;

        MODULEINFO modInfo{};
        if (GetModuleInformation(hProcess, hModules[i], &modInfo, sizeof(modInfo)) && modInfo.SizeOfImage > best.Size)
            best = {static_cast<const uint8_t*>(modInfo.lpBaseOfDll), modInfo.SizeOfImage, modPath};
    }

    if (!best.Base)
        return false;

    image = std::move(best);
    return true;
}

/** The module's image and the ranges to scan, from one enumeration. */
static bool FindImageAndRanges(const char* moduleName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    if (!FindImage(moduleName, image))
        return false;

    ranges.assign(1, ScanRange{image.Base, image.Size});
    return true;
}

#else

static const char* BaseName(const char* path)
{
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

struct ModuleScan
{
    const char* name;               // basename to match, e.g. "libserver.so"
    size_t bestSpan;                // largest module span seen so far (selects the real lib)
    std::vector<ScanRange> ranges;  // PT_LOAD segments of the selected module
    ModuleImage image;              // load bias, span and on-disk path of the selected module
};

// Multiple objects can share the basename "libserver.so" (a loader stub plus the real game
// library), and a substring + first-match scan picks the stub. Match the exact basename and keep
// the largest-span mapping.
static int DlIterateCallback(struct dl_phdr_info* info, size_t /*size*/, void* data)
{
    auto* mod = static_cast<ModuleScan*>(data);
    if (!info->dlpi_name || strcmp(BaseName(info->dlpi_name), mod->name) != 0)
        return 0;

    size_t span = 0;
    std::vector<ScanRange> segments;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
            continue;
        size_t segEnd = phdr.p_vaddr + phdr.p_memsz;
        if (segEnd > span)
            span = segEnd;
        segments.push_back({reinterpret_cast<const uint8_t*>(info->dlpi_addr + phdr.p_vaddr), phdr.p_memsz});
    }

    if (span > mod->bestSpan)
    {
        mod->bestSpan = span;
        mod->ranges = std::move(segments);
        // l_addr, not the first segment's mapped address: ELF symbol values are link-time
        // addresses that must be biased by exactly this to become runtime addresses.
        mod->image = {reinterpret_cast<const uint8_t*>(info->dlpi_addr), span, info->dlpi_name};
    }
    return 0;  // keep iterating; the largest match wins
}

static bool ScanModule(const char* fileName, ModuleScan& mod)
{
    mod = ModuleScan{fileName, 0, {}, {}};
    dl_iterate_phdr(DlIterateCallback, &mod);
    return !mod.ranges.empty();
}

/** The module's image and the ranges to scan, from one walk. */
static bool FindImageAndRanges(const char* moduleName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    ModuleScan mod{};
    if (!ScanModule(moduleName, mod))
        return false;
    image = std::move(mod.image);
    ranges = std::move(mod.ranges);
    return true;
}

static bool FindImage(const char* fileName, ModuleImage& image)
{
    ModuleScan mod{};
    if (!ScanModule(fileName, mod))
        return false;
    image = std::move(mod.image);
    return true;
}

#endif

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
    return FindImage(PlatformModuleName(moduleName).c_str(), image);
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

    auto patternBytes = ParsePattern(pattern);
    const size_t anchor = AnchorOf(patternBytes, FrequenciesOf(image, ranges));
    void* first = nullptr;
    for (const auto& range : ranges)
    {
        const uint8_t* base = range.base;
        size_t size = range.size;
        while (void* hit = ScanMemory(base, size, patternBytes, anchor))
        {
            if (first)
            {
                Log::Warn("SigScanner: Pattern ambiguous in '{}' (2+ matches); refusing it.", fullName);
                return {first, false, std::move(image)};
            }
            first = hit;
            // Resume one byte past the hit to detect a second match.
            const auto* next = static_cast<const uint8_t*>(hit) + 1;
            size -= static_cast<size_t>(next - base);
            base = next;
        }
    }

    if (!first)
        Log::Warn("SigScanner: Pattern not found in '{}'.", fullName);
    return {first, true, std::move(image)};
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
