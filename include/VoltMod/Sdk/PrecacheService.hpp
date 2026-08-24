#pragma once

#include <memory>
#include <string>
#include <vector>

namespace VoltMod::Sdk
{

class PrecacheGameSystem;  // internal (src/Sdk/GameSystem.hpp)
class GameSystemFactory;   // internal

/**
 * @brief Precaches custom resources (particles, models, sound events) by
 * registering a kit-owned game system that receives BuildGameSessionManifest.
 *
 * Paths queued with Add() take effect at the NEXT map load - the engine's
 * resource manifest only exists inside that event. Resources that are not part
 * of the map's own assets must also reach clients (e.g. via a workshop addon),
 * or they precache server-side but render nothing.
 */
class PrecacheService
{
public:
    // Ctor/dtor are out-of-line: inline they would instantiate the unique_ptr
    // destructor of the forward-declared game system in every consumer TU.
    PrecacheService();
    ~PrecacheService();
    PrecacheService(const PrecacheService&) = delete;
    PrecacheService& operator=(const PrecacheService&) = delete;

    /** Resolve the game-system signatures and link into the engine's factory list.
     *  systemName must be unique across plugins - the engine rejects duplicates. */
    bool Initialize(std::string systemName);

    /** Detach from the engine (factory list, event dispatcher, active-systems
     *  vector). Idempotent; must run before the plugin image unloads. */
    void Shutdown();

    /** Queue a resource path (e.g. "particles/foo.vpcf") for the next map load. Dedupes. */
    void Add(std::string resourcePath);

private:
    friend class PrecacheGameSystem;  // reads _resources inside the manifest event

    std::vector<std::string> _resources;
    std::string _systemName;
    std::unique_ptr<PrecacheGameSystem> _system;
    GameSystemFactory* _factory = nullptr;
    void* _eventDispatcher = nullptr;  // CGameSystemEventDispatcher** (internal type)
    void* _gameSystems = nullptr;      // CUtlVector<AddedGameSystem_t>* (internal type)
};

}  // namespace VoltMod::Sdk
