#include "Engine/Memory/LoadedModule.hpp"

#ifdef _WIN32

#include <windows.h>

#include <cstring>
#include <psapi.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace VoltMod
{

/** The file name in @p path, after the last separator. */
static std::string_view BaseName(std::string_view path)
{
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

// Module file names are case-insensitive on Windows, so the match must be too.
static bool SameFileName(std::string_view left, std::string_view right)
{
    return left.size() == right.size() && _strnicmp(left.data(), right.data(), left.size()) == 0;
}

bool FindModuleAndRanges(std::string_view fileName, LoadedModule& module, std::vector<ScanRange>& ranges)
{
    HANDLE process = GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed = 0;

    if (!EnumProcessModules(process, modules, sizeof(modules), &needed))
        return false;

    LoadedModule best;
    for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i)
    {
        char buffer[MAX_PATH];
        const DWORD length = GetModuleFileNameA(modules[i], buffer, sizeof(buffer));
        if (length == 0)
            continue;

        const std::string_view path(buffer, length);
        if (!SameFileName(BaseName(path), fileName))
            continue;

        MODULEINFO info{};
        if (GetModuleInformation(process, modules[i], &info, sizeof(info)) && info.SizeOfImage > best.Size)
            best = {static_cast<const uint8_t*>(info.lpBaseOfDll), info.SizeOfImage, std::string(path)};
    }

    if (!best.Base)
        return false;

    module = std::move(best);
    ranges.assign(1, ScanRange{module.Base, module.Size});
    return true;
}

}  // namespace VoltMod

#endif
