#include "Engine/Memory/SigScanner.hpp"

#include "Engine/Memory/BytePattern.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
// The page-protection probe below names VirtualQuery and the PAGE_* flags directly.
#include <windows.h>
#endif

namespace VoltMod
{

/**
 * Byte frequencies across @p ranges, counted once per module.
 *
 * Process-wide rather than per-load, like the schema field cache: a mapped module's bytes do not
 * change while it is mapped, and every signature scanned against it wants the same answer. Keyed
 * by base address, which is what tells two mapped modules apart. Counted over the ranges rather
 * than the module span because on Linux that span covers unmapped gaps between PT_LOAD segments.
 */
static const ByteHistogram& FrequenciesOf(const LoadedModule& loaded, const std::vector<ScanRange>& ranges)
{
    static std::map<const uint8_t*, ByteHistogram> cache;

    const auto found = cache.find(loaded.Base);
    if (found != cache.end())
        return found->second;

    ByteHistogram counts{};
    for (const auto& range : ranges)
        CountBytes(range.Base, range.Size, counts);

    return cache.emplace(loaded.Base, counts).first->second;
}

std::string PlatformModuleName(const char* moduleName)
{
#ifdef _WIN32
    return std::string(moduleName) + ".dll";
#else
    return std::string("lib") + moduleName + ".so";
#endif
}

bool FindLoadedModule(const char* moduleName, LoadedModule& loaded)
{
    std::vector<ScanRange> ranges;  // unused; the module is what the caller asked for
    return FindModuleAndRanges(PlatformModuleName(moduleName).c_str(), loaded, ranges);
}

ScanResult FindPatternEx(const char* moduleName, const std::string& pattern)
{
    const std::string fullName = PlatformModuleName(moduleName);

    LoadedModule loaded;
    std::vector<ScanRange> ranges;
    if (!FindModuleAndRanges(fullName.c_str(), loaded, ranges))
    {
        Log::Error("SigScanner: Module '{}' not found.", fullName);
        return {};
    }

    const std::vector<PatternByte> bytes = ParsePattern(pattern);
    const size_t anchor = AnchorOf(bytes, FrequenciesOf(loaded, ranges));

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
                return {const_cast<uint8_t*>(first), false, std::move(loaded)};
            }
            first = hit;
            at = static_cast<size_t>(hit - range.Base) + 1;
        }
    }

    if (!first)
        Log::Warn("SigScanner: Pattern not found in '{}'.", fullName);
    return {const_cast<uint8_t*>(first), true, std::move(loaded)};
}

uintptr_t ResolveRelativeAddress(const LoadedModule& loaded, uintptr_t matchAddress, int ripOffset, int ripSize)
{
    if (matchAddress == 0 || !loaded.Base)
        return 0;

    // The displacement itself must be inside the mapping: a pattern that matched near the end of
    // the module, or a rel32At past the instruction, would otherwise read unmapped memory.
    if (!Rel32ReadInBounds(reinterpret_cast<uintptr_t>(loaded.Base), loaded.Size, matchAddress, ripOffset))
        return 0;

    const uintptr_t site = Rel32Site(matchAddress, ripOffset);
    int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(site), sizeof(displacement));
    return Rel32Target(site, displacement, ripSize);
}

/** The committed mapping holding an address: where it ends and what it allows. */
struct MemoryRegion
{
    const uint8_t* End = nullptr;
    bool Readable = false;
    bool Executable = false;
};

/** The mapping holding @p address; empty when nothing is committed there or it is a guard page. */
static std::optional<MemoryRegion> QueryRegion(const void* address)
{
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & PAGE_GUARD) != 0)
        return std::nullopt;

    constexpr DWORD readable = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return MemoryRegion{.End = static_cast<const uint8_t*>(info.BaseAddress) + info.RegionSize,
                        .Readable = (info.Protect & readable) != 0,
                        .Executable = (info.Protect & executable) != 0};
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
            return MemoryRegion{.End = reinterpret_cast<const uint8_t*>(end),
                                .Readable = perms[0] == 'r',
                                .Executable = perms[2] == 'x'};
    }
    return std::nullopt;
#endif
}

bool IsExecutableAddress(const void* address)
{
    if (!address)
        return false;

    const auto region = QueryRegion(address);
    return region && region->Executable;
}

bool IsReadableAddress(const void* address, size_t bytes)
{
    if (!address || bytes == 0)
        return false;

    // The span must end inside this mapping; the next one along may not be mapped at all.
    const auto region = QueryRegion(address);
    return region && region->Readable &&
           bytes <= static_cast<size_t>(region->End - static_cast<const uint8_t*>(address));
}

}  // namespace VoltMod
