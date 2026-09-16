#pragma once

#include "Engine/Memory/LoadedModule.hpp"
#include "Engine/Memory/RelativeAddress.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace VoltMod
{

struct ScanResult
{
    void* Address = nullptr;  // first match, or nullptr
    bool Unique = true;       // false when another match exists
    LoadedModule Module;      // scanned module; Base is null when it was not loaded
};

std::string PlatformModuleName(std::string_view moduleName);

bool FindLoadedModule(std::string_view moduleName, LoadedModule& module);

/**
 * Scan a loaded module for a byte pattern with `?` wildcards. Address holds the first match, while
 * Unique reports whether it is safe to bind.
 */
ScanResult FindPatternEx(std::string_view moduleName, const std::string& pattern);

/**
 * Resolve the 32-bit displacement at `matchAddress + ripOffset` relative to the instruction end.
 *
 * @return 0 when the displacement is not wholly inside the module.
 */
uintptr_t ResolveRelativeAddress(const LoadedModule& module, uintptr_t matchAddress, int ripOffset,
                                 int ripSize = Rel32Size);

/**
 * Whether @p address lies in committed, executable memory.
 *
 * This accepts hook trampolines allocated outside a module while rejecting vtable slots that point
 * at data, such as RTTI, strings, or the tail of a shorter table.
 */
bool IsExecutableAddress(const void* address);

/**
 * Whether the complete span [@p address, @p address + @p bytes) is readable in one mapping.
 *
 * Use this before dereferencing untrusted object bytes. Calling @ref IsExecutableAddress on `p[0]`
 * is already too late because evaluating `p[0]` performs the unsafe read.
 */
bool IsReadableAddress(const void* address, size_t bytes);

}  // namespace VoltMod
