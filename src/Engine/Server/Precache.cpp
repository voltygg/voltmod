#include "Engine/Server/GameSystem.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Server/Precache.hpp>
#include <algorithm>

namespace VoltMod
{

static void RemoveListener(CUtlVector<CUtlVector<IGameSystem*>>& table, IGameSystem* system)
{
    // Each slot is indexed by event id, so an emptied slot stays.
    for (int event = 0; event < table.Count(); ++event)
    {
        auto& listeners = table[event];
        for (int i = listeners.Count() - 1; i >= 0; --i)
        {
            if (listeners[i] == system)
                listeners.Remove(i);
        }
    }
}

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
    _fallbackListeners = _bindings.GameSystemFallbackListeners.Ptr();

    if (!listHead || !_eventDispatcher || !_gameSystems || !_fallbackListeners)
    {
        _eventDispatcher = nullptr;
        _gameSystems = nullptr;
        _fallbackListeners = nullptr;
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

    // Detach the live game system from the current session so nothing calls into this plugin
    // after unload. Removals keep the order: the engine runs systems in list order, and a
    // reordered list broke bot animation after a reload.
    if (auto* gameSystems = static_cast<CUtlVector<AddedGameSystem_t>*>(_gameSystems))
    {
        for (int i = gameSystems->Count() - 1; i >= 0; --i)
        {
            if ((*gameSystems)[i].m_pGameSystem == _system.get())
                gameSystems->Remove(i);
        }
    }

    auto** dispatcherSlot = static_cast<CGameSystemEventDispatcher**>(_eventDispatcher);
    if (dispatcherSlot && *dispatcherSlot && (*dispatcherSlot)->m_funcListeners)
        RemoveListener(*(*dispatcherSlot)->m_funcListeners, _system.get());
    // A listener left here is called after the image unloads, at the next level change.
    if (auto* fallback = static_cast<CUtlVector<CUtlVector<IGameSystem*>>*>(_fallbackListeners))
        RemoveListener(*fallback, _system.get());

    _system.reset();
    _eventDispatcher = nullptr;
    _gameSystems = nullptr;
    _fallbackListeners = nullptr;
}

void Precache::Add(std::string_view resourcePath)
{
    if (resourcePath.empty())
        return;

    if (std::ranges::find(_resources, resourcePath) != _resources.end())
        return;

    _resources.emplace_back(resourcePath);
}

}  // namespace VoltMod
