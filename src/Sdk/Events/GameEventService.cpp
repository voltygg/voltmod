#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Events/GameEventService.hpp>
#include <bit>
#include <playerslot.h>

namespace VoltMod::Sdk
{

using namespace VoltMod::Core;

bool GameEventService::Initialize()
{
    if (void* legacyListener = VoltMod::Detail::Rt().GameData.FindSignature("LegacyGameEventListener"))
        _getLegacyListener = std::bit_cast<GetLegacyGameEventListenerFn>(legacyListener);
    else
        Log::Warn("LegacyGameEventListener signature not found; per-client event delivery unavailable.");

    if (!VoltMod::Detail::Rt().Interfaces.GameEventManager)
    {
        Log::Warn("GameEventService: IGameEventManager2 not available.");
        return false;
    }

    Log::Info("Game event service initialized.");
    return true;
}

IGameEventListener2* GameEventService::GetClientLegacyListener(int slot) const
{
    if (!_getLegacyListener || !Core::IsValidSlot(slot))
        return nullptr;

    return _getLegacyListener(CPlayerSlot(slot));
}

bool GameEventService::ClientListensTo(int slot, const char* eventName) const
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    auto* listener = GetClientLegacyListener(slot);
    if (!mgr || !listener || !eventName)
        return false;

    return mgr->FindListener(listener, eventName);
}

IGameEvent* GameEventService::CreateEvent(const char* name)
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr)
        return nullptr;

    return mgr->CreateEvent(name);
}

bool GameEventService::FireEvent(IGameEvent* event, bool dontBroadcast)
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr || !event)
        return false;

    return mgr->FireEvent(event, dontBroadcast);
}

void GameEventService::FreeEvent(IGameEvent* event)
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    if (mgr && event)
        mgr->FreeEvent(event);
}

Core::Subscription GameEventService::Listen(const char* eventName, EventCallback callback)
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr)
        return {};

    // This attach only serves listens made while a map is live (late load, mid-map Listen);
    // the engine drops it during the next map startup, where OnServerStartup re-attaches.
    if (_registeredEvents.insert(eventName).second)
        mgr->AddListener(this, eventName, true);

    return _listeners.AddOwned({eventName, std::move(callback)});
}

void GameEventService::OnServerStartup()
{
    auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr || _registeredEvents.empty())
        return;

    // Detach first so a listener that did survive is not registered twice (double dispatch).
    mgr->RemoveListener(this);

    int attached = 0;
    for (const auto& name : _registeredEvents)
    {
        if (mgr->AddListener(this, name.c_str(), true))
            ++attached;
        else
            Log::Warn("Game event listener failed to attach: {}.", name);
    }
    Log::Info("Attached {}/{} game event listener(s) at map start.", attached, _registeredEvents.size());
}

void GameEventService::RemoveAllListeners()
{
    if (auto* mgr = VoltMod::Detail::Rt().Interfaces.GameEventManager)
        mgr->RemoveListener(this);  // detaches this listener from every event in one call

    _registeredEvents.clear();
    _listeners.Clear();
}

void GameEventService::FireGameEvent(IGameEvent* event)
{
    if (!event)
        return;

    const char* eventName = event->GetName();
    if (!eventName)
        return;

    // DispatchIf owns the snapshot-and-re-find dance: a handler is free to Listen() or
    // RemoveListener() from inside this call.
    _listeners.DispatchIf([&](const RegisteredListener& l) { return l.Callback && l.EventName == eventName; },
                          [&](RegisteredListener& l) { l.Callback(event); });
}

}  // namespace VoltMod::Sdk
