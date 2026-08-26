#pragma once

#include <VoltMod/Engine/EngineTypes.hpp>

namespace VoltMod
{

/**
 * @brief Centralized holder for all HL2SDK interface pointers.
 *
 * All fields are populated during Plugin::Load() via Metamod's
 * `GET_V_IFACE_ANY` / `GET_V_IFACE_CURRENT` macros.
 *
 * Every field is a pointer, so this header names the SDK interfaces without including
 * any SDK header. A translation unit that calls through one of them includes the SDK
 * header it needs; the rest of the framework - and every plugin that only holds the
 * runtime - stays clear of eiface.h.
 */
struct Interfaces
{
    Interfaces() = default;

    ISource2Server* ServerGameDLL = nullptr;
    ISource2GameClients* ServerGameClients = nullptr;
    INetworkServerService* NetworkServerService = nullptr;
    ISource2GameEntities* GameEntities = nullptr;
    IVEngineServer2* Engine = nullptr;
    IGameEventSystem* GameEventSystem = nullptr;
    INetworkMessages* NetworkMessages = nullptr;
    IGameEventManager2* GameEventManager = nullptr;
    ISchemaSystem* SchemaSystem = nullptr;
    CGameEntitySystem* EntitySystem = nullptr;
    ICvar* CVar = nullptr;
    IGameResourceService* GameResourceService = nullptr;
};

}  // namespace VoltMod
