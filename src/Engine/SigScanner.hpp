#pragma once

#include <cstdint>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace VoltMod
{

struct ScanResult
{
    void* Address = nullptr;  // first match, or nullptr
    bool Unique = true;       // false when the pattern matched more than once
};

/** A loaded module's mapped image. */
struct ModuleImage
{
    const uint8_t* Base = nullptr;  // mapped base address
    size_t Size = 0;                // mapped span in bytes
    std::string Path;               // full path of the file backing the mapping
};

/** Platform file name for a module: "engine2" -> "engine2.dll" / "libengine2.so". */
std::string PlatformModuleName(const char* moduleName);

/** Locate a loaded module by platform-agnostic name. False when it is not mapped. */
bool FindModuleImage(const char* moduleName, ModuleImage& image);

/**
 * Scan a loaded module's memory for a byte pattern (hex string with '?' wildcards).
 * Keeps scanning after the first hit so an ambiguous pattern is reported, not
 * silently taken.
 */
ScanResult FindPatternEx(const char* moduleName, const std::string& pattern);

/** First-match convenience wrapper over FindPatternEx. */
void* FindPattern(const char* moduleName, const std::string& pattern);

/**
 * Resolve a RIP-relative address: reads the 32-bit displacement at addr+ripOffset
 * and computes the absolute target as addr + ripOffset + ripSize + displacement.
 */
uintptr_t ResolveRelativeAddress(uintptr_t addr, int ripOffset, int ripSize);

}  // namespace VoltMod
