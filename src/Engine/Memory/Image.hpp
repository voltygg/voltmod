#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

/** A process mapping and the file backing it. */
struct Image
{
    const uint8_t* Base = nullptr;
    size_t Size = 0;
    std::string Path;

    bool Contains(const void* address) const
    {
        const auto* at = static_cast<const uint8_t*>(address);
        return Base && at >= Base && at < Base + Size;
    }
};

struct ScanRange
{
    const uint8_t* Base;
    size_t Size;
};

/**
 * Locate the exact platform @p fileName and populate its mapping and readable scan ranges.
 *
 * Windows returns the mapped image as one range. Linux returns individual PT_LOAD segments because
 * the full module span may contain unmapped gaps.
 *
 * @return false when the module is not loaded. Both outputs remain unchanged.
 */
bool FindModuleAndRanges(std::string_view fileName, Image& module, std::vector<ScanRange>& ranges);

}  // namespace VoltMod
