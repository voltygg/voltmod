#include "Engine/GameSystem.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Precache.hpp>
#include <algorithm>

namespace VoltMod
{

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

Precache::Precache(const Bindings& bindings) : _bindings(bindings) {}

Precache::~Precache()
{
    Shutdown();
}

Status Precache::Initialize(std::string systemName)
{
    if (_factory)
        return {};

    auto* listHead = static_cast<GameSystemFactory**>(_bindings.GameSystemFactoryList.Ptr());
    _eventDispatcher = _bindings.GameSystemEventDispatcher.Ptr();
    _gameSystems = _bindings.GameSystemList.Ptr();

    if (!listHead || !_eventDispatcher || !_gameSystems)
    {
        _eventDispatcher = nullptr;
        _gameSystems = nullptr;
        return std::unexpected(Error::Unsupported("a game-system address did not bind"));
    }

    _systemName = std::move(systemName);
    _system = std::make_unique<PrecacheGameSystem>(*this);
    _factory = new GameSystemFactory(_systemName.c_str(), _system.get(), listHead);

    Log::Info("Precache: game system '{}' registered (active from the next map load).", _systemName);
    return {};
}

void Precache::Shutdown()
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

void Precache::Add(std::string resourcePath)
{
    if (resourcePath.empty())
        return;

    if (std::ranges::find(_resources, resourcePath) != _resources.end())
        return;

    _resources.push_back(std::move(resourcePath));
}

}  // namespace VoltMod
