#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <bit>
#include <playerslot.h>

namespace VoltMod
{

GameEvents::GameEvents(Interfaces& interfaces, const Bindings& bindings) : _interfaces(interfaces), _bindings(bindings)
{}

GameEvents::~GameEvents()
{
    // The engine must stop dispatching into this listener before the object goes away.
    RemoveAllListeners();
}

Status GameEvents::Initialize()
{
    if (_bindings.LegacyGameEventListener)
        _getLegacyListener = std::bit_cast<GetLegacyGameEventListenerFn>(_bindings.LegacyGameEventListener.Ptr());
    else
        Log::Warn("LegacyGameEventListener signature not found; per-client event delivery unavailable.");

    if (!_interfaces.GameEventManager)
        return std::unexpected(Error::NotReady("IGameEventManager2 not available"));

    Log::Info("Game event service initialized.");
    return {};
}

IGameEventListener2* GameEvents::GetClientLegacyListener(int slot) const
{
    if (!_getLegacyListener || !IsValidSlot(slot))
        return nullptr;

    return _getLegacyListener(CPlayerSlot(slot));
}

bool GameEvents::ClientListensTo(int slot, const char* eventName) const
{
    auto* mgr = _interfaces.GameEventManager;
    auto* listener = GetClientLegacyListener(slot);
    if (!mgr || !listener || !eventName)
        return false;

    return mgr->FindListener(listener, eventName);
}

IGameEvent* GameEvents::CreateEvent(const char* name)
{
    auto* mgr = _interfaces.GameEventManager;
    if (!mgr)
        return nullptr;

    return mgr->CreateEvent(name);
}

bool GameEvents::FireEvent(IGameEvent* event, bool dontBroadcast)
{
    auto* mgr = _interfaces.GameEventManager;
    if (!mgr || !event)
        return false;

    return mgr->FireEvent(event, dontBroadcast);
}

void GameEvents::FreeEvent(IGameEvent* event)
{
    auto* mgr = _interfaces.GameEventManager;
    if (mgr && event)
        mgr->FreeEvent(event);
}

Subscription GameEvents::Add(const char* eventName, EventCallback callback)
{
    auto* mgr = _interfaces.GameEventManager;
    if (!mgr)
        return {};

    // This attach only serves listens made while a map is live (late load, mid-map On<T>);
    // the engine drops it during the next map startup, where OnServerStartup re-attaches.
    if (_registeredEvents.insert(eventName).second)
        mgr->AddListener(this, eventName, true);

    return _listeners.AddOwned({eventName, std::move(callback)});
}

void GameEvents::OnServerStartup()
{
    auto* mgr = _interfaces.GameEventManager;
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

void GameEvents::RemoveAllListeners()
{
    // Idempotent: the runtime calls this explicitly and the destructor calls it again.
    if (auto* mgr = _interfaces.GameEventManager; mgr && !_registeredEvents.empty())
        mgr->RemoveListener(this);  // detaches this listener from every event in one call

    _registeredEvents.clear();
    _listeners.Clear();
}

void GameEvents::FireGameEvent(IGameEvent* event)
{
    if (!event)
        return;

    const char* eventName = event->GetName();
    if (!eventName)
        return;

    // DispatchIf owns the snapshot-and-re-find dance: a handler is free to subscribe or
    // unsubscribe from inside this call.
    _listeners.DispatchIf([&](const RegisteredListener& l) { return l.Callback && l.EventName == eventName; },
                          [&](RegisteredListener& l) { l.Callback(event); });
}

}  // namespace VoltMod
