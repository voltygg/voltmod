#include "Engine/Memory/SigScanner.hpp"

#include "Engine/Memory/BytePattern.hpp"

#include <VoltMod/Core/Log.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
// The page-protection probe uses the Windows API names directly.
#include <windows.h>
#endif

namespace VoltMod
{

/**
 * Byte frequencies are stable for a mapped module, so cache them by base address. Count explicit
 * scan ranges because a Linux module's overall span may include unmapped gaps.
 */
static const ByteHistogram& FrequenciesOf(const Image& module, const std::vector<ScanRange>& ranges)
{
    static std::map<const uint8_t*, ByteHistogram> cache;

    const auto found = cache.find(module.Base);
    if (found != cache.end())
    {
        return found->second;
    }

    ByteHistogram counts{};
    for (const auto& range : ranges)
    {
        CountBytes(range.Base, range.Size, counts);
    }

    return cache.emplace(module.Base, counts).first->second;
}

std::string PlatformModuleName(std::string_view moduleName)
{
#ifdef _WIN32
    return std::format("{}.dll", moduleName);
#else
    return std::format("lib{}.so", moduleName);
#endif
}

bool FindImage(std::string_view moduleName, Image& module)
{
    std::vector<ScanRange> ranges;
    return FindModuleAndRanges(PlatformModuleName(moduleName), module, ranges);
}

ScanResult FindPatternEx(std::string_view moduleName, const std::string& pattern)
{
    const std::string fullName = PlatformModuleName(moduleName);

    Image module;
    std::vector<ScanRange> ranges;
    if (!FindModuleAndRanges(fullName, module, ranges))
    {
        Log::Error("SigScanner: Module '{}' not found.", fullName);
        return {};
    }

    const std::vector<PatternByte> bytes = ParsePattern(pattern);
    const size_t anchor = AnchorOf(bytes, FrequenciesOf(module, ranges));

    const uint8_t* first = nullptr;
    for (const auto& range : ranges)
    {
        // Continue after a hit so an ambiguous pattern is not bound to the first match.
        for (size_t at = 0; at < range.Size;)
        {
            const uint8_t* hit = FindFirst(range.Base + at, range.Size - at, bytes, anchor);
            if (!hit)
            {
                break;
            }

            if (first)
            {
                Log::Warn("SigScanner: Pattern ambiguous in '{}' (2+ matches); refusing it.", fullName);
                return {const_cast<uint8_t*>(first), false, std::move(module)};
            }
            first = hit;
            at = static_cast<size_t>(hit - range.Base) + 1;
        }
    }

    if (!first)
    {
        Log::Warn("SigScanner: Pattern not found in '{}'.", fullName);
    }
    return {const_cast<uint8_t*>(first), true, std::move(module)};
}

uintptr_t ResolveRelativeAddress(const Image& module, uintptr_t matchAddress, int ripOffset, int ripSize)
{
    if (matchAddress == 0 || !module.Base)
    {
        return 0;
    }

    // The displacement must be inside the mapping before it is read.
    if (!Rel32ReadInBounds(reinterpret_cast<uintptr_t>(module.Base), module.Size, matchAddress, ripOffset))
    {
        return 0;
    }

    const uintptr_t site = Rel32Site(matchAddress, ripOffset);
    int32_t displacement = 0;
    std::memcpy(&displacement, reinterpret_cast<const void*>(site), sizeof(displacement));
    return Rel32Target(site, displacement, ripSize);
}

struct MemoryRegion
{
    const uint8_t* End = nullptr;
    bool Readable = false;
    bool Executable = false;
};

static std::optional<MemoryRegion> QueryRegion(const void* address)
{
#ifdef _WIN32
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & PAGE_GUARD) != 0)
    {
        return std::nullopt;
    }

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
        {
            continue;
        }
        if (target >= start && target < end)
        {
            return MemoryRegion{.End = reinterpret_cast<const uint8_t*>(end),
                                .Readable = perms[0] == 'r',
                                .Executable = perms[2] == 'x'};
        }
    }
    return std::nullopt;
#endif
}

bool IsExecutableAddress(const void* address)
{
    if (!address)
    {
        return false;
    }

    const auto region = QueryRegion(address);
    return region && region->Executable;
}

bool IsReadableAddress(const void* address, size_t bytes)
{
    if (!address || bytes == 0)
    {
        return false;
    }

    // Keep the span within one mapping because the next may be unmapped.
    const auto region = QueryRegion(address);
    return region && region->Readable &&
           bytes <= static_cast<size_t>(region->End - static_cast<const uint8_t*>(address));
}

}  // namespace VoltMod
