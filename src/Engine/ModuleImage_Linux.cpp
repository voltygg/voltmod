#include "Engine/ModuleImage.hpp"

#ifndef _WIN32

#include <algorithm>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <utility>
#include <vector>

namespace VoltMod
{

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

bool FindImageAndRanges(const char* fileName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    ModuleScan scan{.Name = fileName};
    dl_iterate_phdr(DlIterateCallback, &scan);
    if (scan.Ranges.empty())
        return false;

    image = std::move(scan.Image);
    ranges = std::move(scan.Ranges);
    return true;
}

}  // namespace VoltMod

#endif
