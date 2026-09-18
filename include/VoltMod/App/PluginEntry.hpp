#pragma once

#include <VoltMod/App/Internal/PluginModule.hpp>
#include <VoltMod/Host/Abi.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>

/**
 * @brief Define the plugin module and the entry point the host resolves.
 *
 * @p PluginClass must derive from VoltMod::Plugin and be constructible from VoltMod::Runtime&.
 * Invoke once, at global namespace scope, in one .cpp, and include this header there only.
 */
#define VOLTMOD_PLUGIN(PluginClass)                                                                                  \
    static auto g_voltmodPlugin = ::VoltMod::Internal::MakePluginModule<PluginClass>();                              \
    namespace KHook                                                                                                  \
    {                                                                                                                \
    KHook::IKHook* __exported__khook = nullptr;                                                                      \
    }                                                                                                                \
    static bool VoltMod_PluginLoad(::VoltMod::IHost* host, char* error, size_t errorSize) noexcept                   \
    {                                                                                                                \
        return host && g_voltmodPlugin.Attach(*host, error, errorSize);                                              \
    }                                                                                                                \
    static void VoltMod_PluginUnload() noexcept                                                                      \
    {                                                                                                                \
        g_voltmodPlugin.Detach();                                                                                    \
    }                                                                                                                \
    static const char* VoltMod_PluginStatus() noexcept                                                               \
    {                                                                                                                \
        return g_voltmodPlugin.StatusJson();                                                                         \
    }                                                                                                                \
    static const ::VoltMod::PluginDescriptor g_voltmodDescriptor{::VoltMod::HostAbiVersion, &VoltMod_PluginLoad,     \
                                                                 &VoltMod_PluginUnload, &VoltMod_PluginStatus};      \
    extern "C" VOLTMOD_EXPORT const ::VoltMod::PluginDescriptor* VoltMod_PluginEntry() noexcept                      \
    {                                                                                                                \
        return &g_voltmodDescriptor;                                                                                 \
    }                                                                                                                \
    static_assert(true, "VOLTMOD_PLUGIN requires a trailing semicolon")
