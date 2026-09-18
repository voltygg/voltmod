#include "Host/Library.hpp"

#include <format>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace VoltMod
{

#if defined(_WIN32)

// What the last Win32 call failed with, without the trailing newline FormatMessage adds.
static std::string LastError()
{
    const DWORD code = GetLastError();
    char* text = nullptr;
    const DWORD length =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, code, 0, reinterpret_cast<char*>(&text), 0, nullptr);

    std::string message = length > 0 && text != nullptr ? std::string(text, length) : std::string("no message");
    if (text != nullptr)
        LocalFree(text);

    while (!message.empty() && (message.back() == '\n' || message.back() == '\r' || message.back() == ' '))
        message.pop_back();

    return std::format("{} (error {})", message, code);
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

Library::~Library()
{
    Close();
}

Library::Library(Library&& other) noexcept : _handle(std::exchange(other._handle, nullptr)) {}

Library& Library::operator=(Library&& other) noexcept
{
    if (this != &other)
    {
        Close();
        _handle = std::exchange(other._handle, nullptr);
    }
    return *this;
}

Result<Library> Library::Open(const std::filesystem::path& path)
{
    void* handle = OpenLibrary(path);
    if (handle == nullptr)
        return std::unexpected(Error::Failed(std::format("cannot load {}: {}", path.string(), LastError())));

    return Library(handle);
}

Result<void*> Library::Symbol(const char* name) const
{
    if (_handle == nullptr)
        return std::unexpected(Error::NotReady(std::format("cannot look up {}: nothing is loaded", name)));

    void* address = FindExport(_handle, name);
    if (address == nullptr)
        return std::unexpected(Error::NotFound(std::format("does not export {}: {}", name, LastError())));

    return address;
}

void Library::Close()
{
    if (_handle == nullptr)
        return;

    CloseLibrary(std::exchange(_handle, nullptr));
}

}  // namespace VoltMod
