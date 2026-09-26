#pragma once

#include "Host/HostStart.hpp"

#include <VoltMod/Host/PluginDescriptor.hpp>
#include <filesystem>
#include <string>
#include <string_view>

/** The engine loads this module as `server_valve` and asks it for the game server's interfaces. */
extern "C" VOLTMOD_EXPORT void* CreateInterface(const char* name, int* returnCode);

namespace VoltMod
{

// CMake passes the host's file name and bin directory, so they cannot drift from the build.
inline constexpr std::string_view PlatformDir = VOLTMOD_BIN_DIR;
inline constexpr std::string_view HostFile = VOLTMOD_HOST_FILE;
#if defined(_WIN32)
inline constexpr std::string_view ServerFile = "server.dll";
#else
inline constexpr std::string_view ServerFile = "libserver.so";
#endif

// Loader.windows.cpp and Loader.linux.cpp each implement these for their platform.

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
