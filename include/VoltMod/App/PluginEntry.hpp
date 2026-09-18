#pragma once

#include <VoltMod/App/AppPlugin.hpp>
#include <VoltMod/BuildInfo.hpp>
#include <VoltMod/Host/Abi.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>

/**
 * @brief Define the plugin instance and the entry point the host resolves.
 *
 * @p PluginClass is your App (see @ref VoltMod::AppPlugin) or a class derived from VoltMod::Plugin.
 * Invoke once, at global namespace scope, in one .cpp, and include this header there only:
 * BuildInfo.hpp changes every commit. The library is not a Metamod plugin, so it defines KHook's
 * dispatch pointer itself; Attach seeds it from the host.
 */
#define VOLTMOD_PLUGIN(PluginClass)                                                                        \
    static ::VoltMod::PluginFor<PluginClass> g_voltmodPlugin;                                              \
    namespace KHook                                                                                        \
    {                                                                                                      \
    KHook::IKHook* __exported__khook = nullptr;                                                            \
    }                                                                                                      \
    static bool VoltMod_PluginLoad(::VoltMod::IHost* host, char* error, size_t errorSize)                  \
    {                                                                                                      \
        static constexpr ::VoltMod::PluginBuild build{                                                     \
            ::VoltMod::BuildInfo::Version, ::VoltMod::BuildInfo::RepoCommit, ::VoltMod::BuildInfo::BuildDate}; \
        return host && g_voltmodPlugin.Attach(*host, build, error, errorSize);                             \
    }                                                                                                      \
    static void VoltMod_PluginUnload()                                                                     \
    {                                                                                                      \
        g_voltmodPlugin.Detach();                                                                          \
    }                                                                                                      \
    static const char* VoltMod_PluginStatus()                                                              \
    {                                                                                                      \
        return g_voltmodPlugin.StatusJson();                                                               \
    }                                                                                                      \
    static const ::VoltMod::PluginDescriptor g_voltmodDescriptor{                                          \
        ::VoltMod::HostAbiVersion, ::VoltMod::BuildInfo::Version, &VoltMod_PluginLoad, &VoltMod_PluginUnload, \
        &VoltMod_PluginStatus};                                                                            \
    extern "C" VOLTMOD_EXPORT const ::VoltMod::PluginDescriptor* VoltMod_PluginEntry()                     \
    {                                                                                                      \
        return &g_voltmodDescriptor;                                                                       \
    }                                                                                                      \
    static_assert(true, "VOLTMOD_PLUGIN requires a trailing semicolon")
