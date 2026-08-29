#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace VoltMod
{

/** A loaded module's mapped image. */
struct ModuleImage
{
    const uint8_t* Base = nullptr;  // mapped base address
    size_t Size = 0;                // mapped span in bytes
    std::string Path;               // full path of the file backing the mapping
};

// A mapped region to scan: the whole image on Windows, one PT_LOAD segment on Linux (so we never
// read across an unmapped `-z separate-code` gap).
struct ScanRange
{
    const uint8_t* Base;
    size_t Size;
};

/**
 * The module's image and the ranges to scan, from one enumeration of the process.
 *
 * @param fileName the platform file name, as @ref PlatformModuleName spells it.
 * @return false when no such module is mapped, leaving both outputs untouched.
 *
 * Implemented once per platform - `ModuleImage_Windows.cpp` and `ModuleImage_Linux.cpp` - because
 * the two ask the loader entirely different questions.
 */
bool FindImageAndRanges(const char* fileName, ModuleImage& image, std::vector<ScanRange>& ranges);

}  // namespace VoltMod
