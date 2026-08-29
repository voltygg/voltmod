#include "Engine/ModuleImage.hpp"

#ifdef _WIN32

#include <cstring>
#include <psapi.h>
#include <utility>
#include <vector>

namespace VoltMod
{

bool FindImageAndRanges(const char* fileName, ModuleImage& image, std::vector<ScanRange>& ranges)
{
    HANDLE process = GetCurrentProcess();
    HMODULE modules[1024];
    DWORD needed = 0;

    if (!EnumProcessModules(process, modules, sizeof(modules), &needed))
        return false;

    ModuleImage best;
    for (DWORD i = 0; i < needed / sizeof(HMODULE); ++i)
    {
        char path[MAX_PATH];
        if (!GetModuleFileNameA(modules[i], path, sizeof(path)))
            continue;

        const char* baseName = strrchr(path, '\\');
        if (!baseName)
            baseName = strrchr(path, '/');
        baseName = baseName ? baseName + 1 : path;

        if (_stricmp(baseName, fileName) != 0)
            continue;

        MODULEINFO info{};
        if (GetModuleInformation(process, modules[i], &info, sizeof(info)) && info.SizeOfImage > best.Size)
            best = {static_cast<const uint8_t*>(info.lpBaseOfDll), info.SizeOfImage, path};
    }

    if (!best.Base)
        return false;

    image = std::move(best);
    ranges.assign(1, ScanRange{image.Base, image.Size});
    return true;
}

}  // namespace VoltMod

#endif
