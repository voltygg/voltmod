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
class Library
{
public:
    Library() = default;
    ~Library();

    Library(Library&& other) noexcept;
    Library& operator=(Library&& other) noexcept;

    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;

    /** Load @p path. The error carries the operating system's own message. */
    static Result<Library> Open(const std::filesystem::path& path);

    explicit operator bool() const { return _handle != nullptr; }

    /** The address @p name exports, or an error naming it. */
    Result<void*> Symbol(const char* name) const;

    /** Close now rather than at destruction. */
    void Close();

private:
    explicit Library(void* handle) : _handle(handle) {}

    void* _handle = nullptr;
};

}  // namespace VoltMod
