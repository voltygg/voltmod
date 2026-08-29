#pragma once

#include "Engine/ModuleImage.hpp"

#include <VoltMod/Engine/RelativeAddress.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace VoltMod
{

struct ScanResult
{
    void* Address = nullptr;  // first match, or nullptr
    bool Unique = true;       // false when the pattern matched more than once
    ModuleImage Image;        // the module that was scanned; Base is null when it is not mapped
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
 * Resolve a RIP-relative address inside @p image: reads the 32-bit displacement at
 * @p matchAddress + @p ripOffset and returns the absolute target
 * (@p matchAddress + @p ripOffset + @p ripSize + displacement).
 *
 * @return 0 when the displacement does not lie wholly inside the mapped image, which is the one
 *         failure that would otherwise be a read past the mapping rather than a wrong answer.
 */
uintptr_t ResolveRelativeAddress(const ModuleImage& image, uintptr_t matchAddress, int ripOffset,
                                 int ripSize = Rel32Size);

/**
 * True when @p address lies in committed, executable memory (page protection on Windows,
 * `/proc/self/maps` on Linux).
 *
 * Deliberately not "inside this module's code section": a vtable slot another plugin has already
 * hooked points at a SourceHook trampoline in allocated memory, which is code and is correct. What
 * this rules out is a slot holding data - RTTI, a string, the tail of a shorter table - which is
 * what a drifted class name or an index past the end of the real table produces.
 */
bool IsExecutableAddress(const void* address);

/**
 * True when @p bytes bytes starting at @p address can be read without faulting.
 *
 * For validating a pointer *before* dereferencing it. @ref IsExecutableAddress answers a question
 * about the value at an address, so asking it about `p[0]` has already dereferenced `p` - which is
 * a crash when `p` came out of arbitrary object bytes rather than from a known-good field.
 *
 * The span must lie inside one mapping: the next one along may be unmapped.
 */
bool IsReadableAddress(const void* address, size_t bytes);

}  // namespace VoltMod
