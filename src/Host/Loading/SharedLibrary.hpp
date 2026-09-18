#pragma once

#include <VoltMod/Core/Result.hpp>
#include <filesystem>

namespace VoltMod
{

/**
 * @brief One shared library the host has loaded itself, closed when it goes out of scope.
 *
 * Move-only, because two owners would close the same handle twice. The host keeps a plugin's
 * library alive until the plugin's Unload has returned: every hook thunk and subscription closure
 * is code inside it.
 */
class SharedLibrary
{
public:
    SharedLibrary() = default;
    ~SharedLibrary();

    SharedLibrary(SharedLibrary&& other) noexcept;
    SharedLibrary& operator=(SharedLibrary&& other) noexcept;

    SharedLibrary(const SharedLibrary&) = delete;
    SharedLibrary& operator=(const SharedLibrary&) = delete;

    /** Load @p path. The error carries the operating system's own message. */
    static Result<SharedLibrary> Open(const std::filesystem::path& path);

    /** The address @p name exports, or an error naming it. */
    Result<void*> Symbol(const char* name) const;

    /** Close now rather than at destruction. */
    void Close();

private:
    explicit SharedLibrary(void* handle) : _handle(handle) {}

    void* _handle = nullptr;
};

}  // namespace VoltMod
