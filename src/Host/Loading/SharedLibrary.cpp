#include "Host/Loading/SharedLibrary.hpp"

#include <format>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>

#include <system_error>
#else
#include <dlfcn.h>
#endif

namespace VoltMod
{

#if defined(_WIN32)

static std::string LastError()
{
    const DWORD code = GetLastError();
    return std::format("{} (error {})", std::system_category().message(static_cast<int>(code)), code);
}

static void* OpenLibrary(const std::filesystem::path& path)
{
    return LoadLibraryW(path.c_str());
}

static void* FindExport(void* handle, const char* name)
{
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), name));
}

static void CloseLibrary(void* handle)
{
    FreeLibrary(static_cast<HMODULE>(handle));
}

#else

static std::string LastError()
{
    const char* message = dlerror();
    return message != nullptr ? std::string(message) : std::string("no message");
}

static void* OpenLibrary(const std::filesystem::path& path)
{
    // RTLD_LOCAL keeps each plugin's static copy of the SDK to itself; RTLD_NOW reports a missing
    // symbol here instead of at the first call into it.
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
}

static void* FindExport(void* handle, const char* name)
{
    return dlsym(handle, name);
}

static void CloseLibrary(void* handle)
{
    dlclose(handle);
}

#endif

SharedLibrary::~SharedLibrary()
{
    Close();
}

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept : _handle(std::exchange(other._handle, nullptr)) {}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept
{
    if (this != &other)
    {
        Close();
        _handle = std::exchange(other._handle, nullptr);
    }
    return *this;
}

Result<SharedLibrary> SharedLibrary::Open(const std::filesystem::path& path)
{
    void* handle = OpenLibrary(path);
    if (handle == nullptr)
        return std::unexpected(Error::Failed(std::format("cannot load {}: {}", path.string(), LastError())));

    return SharedLibrary(handle);
}

Result<void*> SharedLibrary::Symbol(const char* name) const
{
    if (_handle == nullptr)
        return std::unexpected(Error::NotReady(std::format("cannot look up {}: nothing is loaded", name)));

    void* address = FindExport(_handle, name);
    if (address == nullptr)
        return std::unexpected(Error::NotFound(std::format("does not export {}: {}", name, LastError())));

    return address;
}

void SharedLibrary::Close()
{
    if (_handle == nullptr)
        return;

    CloseLibrary(std::exchange(_handle, nullptr));
}

}  // namespace VoltMod
