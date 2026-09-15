#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/** A module mapped into this process: where it starts, how far it spans, and the file behind it. */
struct LoadedModule
{
    const uint8_t* Base = nullptr;  // mapped base address
    size_t Size = 0;                // mapped span in bytes
    std::string Path;               // full path of the file backing the mapping

    /** Whether @p address lies inside this mapping. */
    bool Contains(const void* address) const
    {
        const auto* at = static_cast<const uint8_t*>(address);
        return Base && at >= Base && at < Base + Size;
    }
};

// Scan the whole module on Windows and each PT_LOAD segment on Linux to avoid unmapped gaps.
struct ScanRange
{
    const uint8_t* Base;
    size_t Size;
};

/**
 * Enumerate @p fileName once, returning where it is loaded and its scan ranges.
 *
 * @param fileName platform file name, as @ref PlatformModuleName spells it.
 * @return false when the module is not loaded; both outputs remain unchanged.
 *
 * Each platform has a separate implementation because its loader exposes different metadata.
 */
bool FindModuleAndRanges(const char* fileName, LoadedModule& loaded, std::vector<ScanRange>& ranges);

}  // namespace VoltMod
