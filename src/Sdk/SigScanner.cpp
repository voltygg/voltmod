#include "Sdk/SigScanner.hpp"

#include <CS2Kit/Core/Log.hpp>
#include <sstream>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <psapi.h>
#else
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#endif

namespace CS2Kit::Sdk
{

using namespace CS2Kit::Core;

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
            bytes.push_back({static_cast<uint8_t>(std::stoul(token, nullptr, 16)), false});
        }
    }
    return bytes;
}

static void* ScanMemory(const uint8_t* base, size_t size, const std::vector<PatternByte>& pattern)
{
    if (pattern.empty() || size < pattern.size())
        return nullptr;

    size_t scanEnd = size - pattern.size();
    for (size_t i = 0; i <= scanEnd; ++i)
    {
        bool found = true;
        for (size_t j = 0; j < pattern.size(); ++j)
        {
            if (!pattern[j].wildcard && base[i + j] != pattern[j].value)
            {
                found = false;
                break;
            }
        }
        if (found)
            return const_cast<uint8_t*>(base + i);
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

static bool GetScanRanges(const char* moduleName, std::vector<ScanRange>& ranges)
{
    ModuleImage image;
    if (!FindImage(moduleName, image))
        return false;

    ranges.push_back({image.Base, image.Size});
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

static bool GetScanRanges(const char* moduleName, std::vector<ScanRange>& ranges)
{
    ModuleScan mod{};
    if (!ScanModule(moduleName, mod))
        return false;
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

    std::vector<ScanRange> ranges;
    if (!GetScanRanges(fullName.c_str(), ranges))
    {
        Log::Error("SigScanner: Module '{}' not found.", fullName);
        return {};
    }

    auto patternBytes = ParsePattern(pattern);
    void* first = nullptr;
    for (const auto& range : ranges)
    {
        const uint8_t* base = range.base;
        size_t size = range.size;
        while (void* hit = ScanMemory(base, size, patternBytes))
        {
            if (first)
            {
                Log::Warn("SigScanner: Pattern ambiguous in '{}' (2+ matches); using the first.", fullName);
                return {first, false};
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
    return {first, true};
}

void* FindPattern(const char* moduleName, const std::string& pattern)
{
    return FindPatternEx(moduleName, pattern).Address;
}

uintptr_t ResolveRelativeAddress(uintptr_t addr, int ripOffset, int ripSize)
{
    if (addr == 0)
        return 0;

    int32_t relative = *reinterpret_cast<int32_t*>(addr + ripOffset);
    return addr + ripSize + relative;
}

}  // namespace CS2Kit::Sdk
