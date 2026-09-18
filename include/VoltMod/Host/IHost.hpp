#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Host/IHostEvents.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <VoltMod/Host/IHostServices.hpp>
#include <cstdint>
#include <string_view>

namespace VoltMod
{

/**
 * @brief What the host hands one plugin at load: that plugin's own view of the host.
 *
 * Everything reachable from here belongs to the host and outlives the plugin. Nothing here
 * transfers ownership. Game thread only.
 */
struct IHost
{
    /** The plugin's name: its manifest name and directory under `addons/voltmod/plugins/`. */
    virtual std::string_view Name() const = 0;

    /** The host is the Metamod plugin and shares its own API pointer unchanged. */
    virtual SourceMM::ISmmAPI* Metamod() const = 0;
    /** Each module has its own `KHook::__exported__khook`; a plugin seeds it from this before hooking. */
    virtual KHook::IKHook* HookDispatcher() const = 0;

    virtual IHostEvents& Events() = 0;
    virtual IHostServices& Services() = 0;
    /** Null when the host resolved no gamedata. */
    virtual IHostGameData* GameData() const = 0;

    /** False when another plugin holds @p name, which the host logs naming both. Released with the plugin. */
    virtual bool RegisterCommand(std::string_view name) = 0;

    /** Print one line under this plugin's `logTag`. @p level is a @ref LogLevel. */
    virtual void WriteLog(uint8_t level, std::string_view text) = 0;
    /** Lines below this are dropped; `volt log <name> <level>` changes it. */
    virtual uint8_t MinLogLevel() const = 0;

    /** The hash of the baked schema layout the host compared with the live game. */
    virtual uint64_t SchemaLayoutStamp() const = 0;
    virtual bool SchemaVerified() const = 0;

protected:
    ~IHost() = default;
};

}  // namespace VoltMod
