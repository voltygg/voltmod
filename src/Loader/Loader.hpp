#pragma once

#include "Host/HostStart.hpp"

#include <VoltMod/Host/PluginDescriptor.hpp>
#include <filesystem>
#include <string>

/** The engine loads this module as `server_valve` and asks it for the game server's interfaces. */
extern "C" VOLTMOD_EXPORT void* CreateInterface(const char* name, int* returnCode);

namespace VoltMod
{

#if defined(_WIN32)
inline constexpr const char* PlatformDir = "win64";
inline constexpr const char* ServerFile = "server.dll";
inline constexpr const char* HostFile = "voltmod.dll";
#else
inline constexpr const char* PlatformDir = "linuxsteamrt64";
inline constexpr const char* ServerFile = "libserver.so";
inline constexpr const char* HostFile = "voltmod.so";
#endif

// Loader_Windows.cpp and Loader_Linux.cpp each implement these for their platform.

/** Null on failure, with the reason in @ref LastError. */
void* OpenModule(const std::filesystem::path& path);
void* FindExport(void* module, const char* name);
/** Why the last OpenModule or FindExport failed. */
std::string LastError();

/** This module's own file. */
std::filesystem::path OwnPath();

/** `CreateInterface` of the loaded module whose image holds @p address. */
InterfaceFactory FactoryAt(const void* address);

}  // namespace VoltMod
