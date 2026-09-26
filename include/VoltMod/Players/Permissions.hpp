#pragma once

#include <cstdint>
#include <string_view>

namespace VoltMod
{

/**
 * @brief The server's permission source, published by one plugin (admin-system) through
 * `runtime.Exchange`.
 *
 * Every runtime's default `Policy::HasPermission` asks it on each call, so `.Permission("x")` works
 * in any plugin while the publisher is loaded, and denies while it is not. Any change to this vtable
 * or to what a parameter means bumps the /N in InterfaceName.
 */
struct IPermissions
{
    static constexpr std::string_view InterfaceName = "voltmod.IPermissions/1";

    /** Whether @p steamId holds @p permission. */
    virtual bool HasPermission(int64_t steamId, std::string_view permission) = 0;

protected:
    // Consumers borrow; they never own or delete.
    ~IPermissions() = default;
};

}  // namespace VoltMod
