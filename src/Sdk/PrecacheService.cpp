#include "Sdk/GameSystem.hpp"

#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/PrecacheService.hpp>
#include <algorithm>

namespace CS2Kit::Sdk
{
using namespace CS2Kit::Core;

GS_EVENT_MEMBER(PrecacheGameSystem, BuildGameSessionManifest)
{
    if (!msg->m_pResourceManifest)
        return;

    // The manifest is not long-lived; every resource must be added inside this event.
    for (const auto& path : _owner._resources)
        msg->m_pResourceManifest->AddResource(path.c_str());

    if (!_owner._resources.empty())
        Log::Info("Precache: added {} resource(s) to the session manifest.", _owner._resources.size());
}

PrecacheService::PrecacheService() = default;

PrecacheService::~PrecacheService()
{
    Shutdown();
}

bool PrecacheService::Initialize(std::string systemName)
{
    if (_factory)
        return true;

    auto& gameData = CS2Kit::Detail::Rt().GameData;

    auto* listHead = static_cast<GameSystemFactory**>(gameData.ResolveSignature("IGameSystem_InitAllSystems_pFirst"));
    _eventDispatcher = gameData.ResolveSignature("IGameSystem_LoopPostInitAllSystems_pEventDispatcher");
    _gameSystems = gameData.ResolveSignature("IGameSystem_LoopDestroyAllSystems_s_GameSystems");

    if (!listHead || !_eventDispatcher || !_gameSystems)
    {
        Log::Warn("Precache: game-system signature(s) unresolved; resource precaching is unavailable.");
        _eventDispatcher = nullptr;
        _gameSystems = nullptr;
        return false;
    }

    _systemName = std::move(systemName);
    _system = std::make_unique<PrecacheGameSystem>(*this);
    _factory = new GameSystemFactory(_systemName.c_str(), _system.get(), listHead);

    Log::Info("Precache: game system '{}' registered (active from the next map load).", _systemName);
    return true;
}

void PrecacheService::Shutdown()
{
    if (!_factory)
        return;

    // Unlink the factory so future InitAllSystems passes no longer see us.
    _factory->Unregister();
    delete _factory;
    _factory = nullptr;

    // Detach the live game system from the current session so nothing calls into
    // this plugin after unload. Both structures may already be empty when the
    // server itself is shutting down. Recipe mirrors CS2Fixes' UnregisterGameSystem.
    if (auto* gameSystems = static_cast<CUtlVector<AddedGameSystem_t>*>(_gameSystems))
    {
        for (int i = gameSystems->Count() - 1; i >= 0; --i)
        {
            if ((*gameSystems)[i].m_pGameSystem == _system.get())
                gameSystems->FastRemove(i);
        }
    }

    auto** dispatcherSlot = static_cast<CGameSystemEventDispatcher**>(_eventDispatcher);
    if (dispatcherSlot && *dispatcherSlot && (*dispatcherSlot)->m_funcListeners)
    {
        auto& funcListeners = *(*dispatcherSlot)->m_funcListeners;
        for (int i = funcListeners.Count() - 1; i >= 0; --i)
        {
            auto& listeners = funcListeners[i];
            for (int j = listeners.Count() - 1; j >= 0; --j)
            {
                if (listeners[j] == _system.get())
                    listeners.FastRemove(j);
            }

            if (!listeners.Count())
                funcListeners.FastRemove(i);
        }
    }

    _system.reset();
    _eventDispatcher = nullptr;
    _gameSystems = nullptr;
}

void PrecacheService::Add(std::string resourcePath)
{
    if (resourcePath.empty())
        return;

    if (std::ranges::find(_resources, resourcePath) != _resources.end())
        return;

    _resources.push_back(std::move(resourcePath));
}

}  // namespace CS2Kit::Sdk
