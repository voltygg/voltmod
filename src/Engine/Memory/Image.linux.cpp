#include "Engine/Memory/Image.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <link.h>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

static std::string_view BaseName(std::string_view path)
{
    const size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

struct ModuleScan
{
    std::string_view Name;          // basename to match, e.g. "libserver.so"
    size_t BestSpan = 0;            // largest span selects the real module
    std::vector<ScanRange> Ranges;  // PT_LOAD segments of the selected module
    Image Module;                   // selected module mapping and path
};

// Multiple objects can share a basename. Match it exactly and keep the largest mapping to avoid
// selecting a loader stub.
static int DlIterateCallback(struct dl_phdr_info* info, size_t /*size*/, void* data)
{
    auto* scan = static_cast<ModuleScan*>(data);
    if (!info->dlpi_name || BaseName(info->dlpi_name) != scan->Name)
    {
        return 0;
    }

    size_t span = 0;
    std::vector<ScanRange> segments;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const auto& phdr = info->dlpi_phdr[i];
        if (phdr.p_type != PT_LOAD || phdr.p_memsz == 0)
        {
            continue;
        }

        span = std::max(span, static_cast<size_t>(phdr.p_vaddr + phdr.p_memsz));
        segments.push_back({reinterpret_cast<const uint8_t*>(info->dlpi_addr + phdr.p_vaddr), phdr.p_memsz});
    }

    if (span > scan->BestSpan)
    {
        scan->BestSpan = span;
        scan->Ranges = std::move(segments);
        // ELF symbol values are link-time addresses, so apply the loader's l_addr bias.
        scan->Module = {reinterpret_cast<const uint8_t*>(info->dlpi_addr), span, info->dlpi_name};
    }
    return 0;  // continue so the largest match wins
}

bool FindModuleAndRanges(std::string_view fileName, Image& module, std::vector<ScanRange>& ranges)
{
    ModuleScan scan{.Name = fileName};
    dl_iterate_phdr(DlIterateCallback, &scan);
    if (scan.Ranges.empty())
    {
        return false;
    }

    module = std::move(scan.Module);
    ranges = std::move(scan.Ranges);
    return true;
}

}  // namespace VoltMod
