#include "Loader/Loader.hpp"

#ifdef _WIN32

#include <windows.h>

#include <iterator>
#include <system_error>

namespace VoltMod
{

void* OpenModule(const std::filesystem::path& path)
{
    return LoadLibraryW(path.c_str());
}

void* FindExport(void* module, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
}

std::string LastError()
{
    return std::system_category().message(static_cast<int>(GetLastError()));
}

/** The loaded module whose image holds @p address, without taking a reference to it. */
static HMODULE ModuleAt(const void* address)
{
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(address), &module);
    return module;
}

std::filesystem::path OwnPath()
{
    static const char inThisModule = 0;
    wchar_t path[4096];
    const DWORD length = GetModuleFileNameW(ModuleAt(&inThisModule), path, static_cast<DWORD>(std::size(path)));
    return std::filesystem::path(path, path + length);
}

InterfaceFactory FactoryAt(const void* address)
{
    HMODULE module = ModuleAt(address);
    return module != nullptr ? reinterpret_cast<InterfaceFactory>(GetProcAddress(module, "CreateInterface")) : nullptr;
}

}  // namespace VoltMod

#endif
