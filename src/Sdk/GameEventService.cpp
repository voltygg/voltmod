#include <CS2Kit/Core/Log.hpp>
#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <playerslot.h>
#include <vector>

namespace CS2Kit::Sdk
{

using namespace CS2Kit::Core;

bool GameEventService::Initialize()
{
    if (void* legacyListener = CS2Kit::Detail::Rt().GameData.FindSignature("LegacyGameEventListener"))
        _getLegacyListener = std::bit_cast<GetLegacyGameEventListenerFn>(legacyListener);
    else
        Log::Warn("LegacyGameEventListener signature not found; per-client event delivery unavailable.");

    if (!CS2Kit::Detail::Rt().Interfaces.GameEventManager)
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
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
    auto* listener = GetClientLegacyListener(slot);
    if (!mgr || !listener || !eventName)
        return false;

    return mgr->FindListener(listener, eventName);
}

IGameEvent* GameEventService::CreateEvent(const char* name)
{
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr)
        return nullptr;

    return mgr->CreateEvent(name);
}

bool GameEventService::FireEvent(IGameEvent* event, bool dontBroadcast)
{
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
    if (!mgr || !event)
        return false;

    return mgr->FireEvent(event, dontBroadcast);
}

void GameEventService::FreeEvent(IGameEvent* event)
{
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
    if (mgr && event)
        mgr->FreeEvent(event);
}

Core::Subscription GameEventService::Listen(const char* eventName, EventCallback callback)
{
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
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
    auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
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
    if (auto* mgr = CS2Kit::Detail::Rt().Interfaces.GameEventManager)
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

    // Snapshot the IDs rather than iterating live: a handler is free to Listen() or
    // RemoveListener(), which rehashes the registry and invalidates the iteration. Combat events
    // fire hundreds of times a second, so the snapshot stays on the stack until it has to grow.
    constexpr size_t InlineCapacity = 16;
    std::array<uint64_t, InlineCapacity> inlineIds{};
    std::vector<uint64_t> overflowIds;
    size_t matched = 0;
    for (const auto& [id, listener] : _listeners.Items())
    {
        if (listener.EventName != eventName || !listener.Callback)
            continue;
        if (matched < InlineCapacity)
            inlineIds[matched] = id;
        else
            overflowIds.push_back(id);
        ++matched;
    }

    // Re-find by ID: an earlier handler in this batch may have removed this listener. Copy the
    // callback out before invoking - running it can destroy the stored one.
    const auto fire = [&](uint64_t id) {
        const RegisteredListener* listener = _listeners.Find(id);
        if (!listener || !listener->Callback)
            return;

        EventCallback callback = listener->Callback;
        callback(event);
    };

    for (size_t i = 0; i < std::min(matched, InlineCapacity); ++i)
        fire(inlineIds[i]);
    for (uint64_t id : overflowIds)
        fire(id);
}

}  // namespace CS2Kit::Sdk
