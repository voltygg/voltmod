#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>
#include <cstddef>

namespace VoltMod
{

/** A module's exported `CreateInterface`. */
using InterfaceFactory = void* (*)(const char* name, int* returnCode);

/**
 * @brief What the loader hands the host when the game server starts.
 *
 * The loader and the host always ship together, so this carries no version. Everything it points
 * to lives as long as the process.
 */
struct HostStart
{
    InterfaceFactory EngineFactory = nullptr;
    InterfaceFactory ServerFactory = nullptr;  ///< the game's own server library, never a loader in front of it
    KHook::IKHook* HookDispatcher = nullptr;   ///< the loader's KHook, which every module's hooks go through
    const char* GameDir = nullptr;             ///< the absolute `csgo` directory
};

/** Start the host. On false, it wrote why into @p error, which holds @p errorSize bytes. */
using HostStartFn = bool (*)(const HostStart* start, char* error, size_t errorSize);
/** Unload every plugin and remove the host's hooks. */
using HostStopFn = void (*)();

inline constexpr const char* HostStartName = "VoltMod_HostStart";
inline constexpr const char* HostStopName = "VoltMod_HostStop";

}  // namespace VoltMod
