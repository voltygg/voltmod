#pragma once

#include <CS2Kit/Core/ILogger.hpp>

// Forward declaration for Metamod API
namespace SourceMM
{
class ISmmAPI;
}
using SourceMM::ISmmAPI;

namespace CS2Kit::App
{
class Services;
}

namespace CS2Kit
{

/**
 * @brief Initialization parameters for CS2Kit.
 * All fields are optional - CS2Kit provides sensible defaults.
 */
struct InitParams
{
    const char* LogPrefix = "CS2Kit";    ///< Prefix for console log messages (e.g., "[MyPlugin]")
    const char* GameDataPath = nullptr;  ///< Override path to signatures.jsonc. If null, uses built-in gamedata.
    Core::ILogger* Logger = nullptr;     ///< Optional custom logger. If null, uses built-in ConsoleLogger.
};

/**
 * @brief Initialize all CS2Kit subsystems.
 *
 * Resolves all required SDK interfaces via Metamod's ISmmAPI, loads gamedata,
 * and initializes internal subsystems (schema, entities, events, menus, etc.).
 *
 * Call from Plugin::Load().
 *
 * @param ismm     Metamod API pointer (from Plugin::Load)
 * @param error    Error buffer for failure messages
 * @param maxlen   Size of the error buffer
 * @param services The plugin-owned service container to populate and initialize.
 * @param params   Optional configuration (log prefix, custom logger, gamedata path override)
 * @return true on success, false if a critical subsystem failed to initialize.
 */
bool Initialize(ISmmAPI* ismm, char* error, size_t maxlen, App::Services& services, const InitParams& params = {});

/**
 * @brief Shut down all CS2Kit subsystems.
 * Call from Plugin::Unload().
 */
void Shutdown(App::Services& services);

/**
 * @brief Process one game frame. Drives Scheduler and MenuManager.
 * Call from Hook_GameFrame().
 */
void OnGameFrame(App::Services& services);

/**
 * @brief Clean up state when a player disconnects.
 * Call from Hook_ClientDisconnect().
 */
void OnPlayerDisconnect(App::Services& services, int slot);

}  // namespace CS2Kit
