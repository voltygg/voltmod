#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/HostTypes.hpp>
#include <cstdint>

namespace VoltMod
{

/**
 * @brief What the host hands one plugin at load.
 *
 * Everything reachable from here belongs to the host and outlives the plugin. Nothing here
 * transfers ownership.
 */
struct IHost
{
    static constexpr const char* InterfaceName = "VoltMod.IHost";

    /** Only ever @ref HostAbiVersion: the host refuses a plugin built against anything else
     *  before calling Load. Offered so a plugin can log what it attached to. */
    virtual uint32_t AbiVersion() const = 0;

    virtual HostString Name() const = 0;
    /** `addons/<name>`, relative to the server's base directory. */
    virtual HostString HomeDirectory() const = 0;

    /** Metamod's API. The host is the Metamod plugin and shares its own pointer unchanged, so a
     *  plugin resolves engine interfaces exactly as it did when it was one. */
    virtual SourceMM::ISmmAPI* Metamod() const = 0;
    /** KHook's dispatcher. Each module carries its own `KHook::__exported__khook`, and a plugin
     *  seeds its copy from this before installing any hook. */
    virtual KHook::IKHook* Detours() const = 0;

    /** One of the host's own interfaces by `InterfaceName`, or nullptr. */
    virtual void* GetInterface(HostString name) const = 0;

    /** Take @p name for this plugin's console commands. False when another plugin already holds
     *  it, which the host logs naming both. Released with the plugin. */
    virtual bool ClaimCommand(HostString name) = 0;

protected:
    ~IHost() = default;
};

}  // namespace VoltMod
