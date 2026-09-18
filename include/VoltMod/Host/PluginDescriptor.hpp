#pragma once

#include <VoltMod/Host/IHost.hpp>
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define VOLTMOD_EXPORT __declspec(dllexport)
#else
#define VOLTMOD_EXPORT __attribute__((visibility("default")))
#endif

namespace VoltMod
{

/** What a plugin library exports. The host checks @ref AbiVersion before it reads anything else. */
struct PluginDescriptor
{
    uint32_t AbiVersion;

    /** The build stamp, "<plugin.json version>+<short-sha>[-dirty]". */
    const char* Version;

    /** Attach to @p host. On false, write why into @p error, which holds @p errorSize bytes. */
    bool (*Load)(IHost* host, char* error, size_t errorSize);

    /** Release everything Load took. The host frees the library only after this returns. */
    void (*Unload)();

    /** This plugin's status as JSON it owns, valid until the next call. */
    const char* (*Status)();
};

/** The one symbol the host resolves in a plugin library. */
inline constexpr const char* PluginEntryName = "VoltMod_PluginEntry";

}  // namespace VoltMod
