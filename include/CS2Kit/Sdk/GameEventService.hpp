#pragma once

#include <igameevents.h>

#include <CS2Kit/Core/CallbackRegistry.hpp>
#include <cstdint>
#include <functional>
#include <set>
#include <string>

class CPlayerSlot;

namespace CS2Kit::Sdk
{

/**
 * @brief Wrapper for IGameEventManager2 providing event creation, firing, and listener registration.
 */
class GameEventService : public IGameEventListener2
{
public:
    GameEventService() = default;

    bool Initialize();

    IGameEvent* CreateEvent(const char* name);
    bool FireEvent(IGameEvent* event, bool dontBroadcast = false);
    void FreeEvent(IGameEvent* event);

    using EventCallback = std::function<void(IGameEvent*)>;
    uint64_t Listen(const char* eventName, EventCallback callback);

    /** Typed listen: @p TEvent is one of @ref CS2Kit::Sdk::Events (carries Name + From).
     *  The handler receives the decoded struct; the raw-IGameEvent overload above stays as
     *  the escape hatch for unmodeled events. */
    template <class TEvent>
    uint64_t Listen(std::function<void(const TEvent&)> handler)
    {
        return Listen(TEvent::Name, [h = std::move(handler)](IGameEvent* e) {
            if (e)
                h(TEvent::From(*e));
        });
    }

    void RemoveListener(uint64_t id);

    /** @brief Remove all listeners and deregister from the engine. Called by CS2Kit::Shutdown() (avoids
     * double-registration on reload). */
    void RemoveAllListeners();

    /**
     * @brief Re-attach every listener to the engine. Called by the kit's StartupServer hook on
     * every map start.
     *
     * AddListener succeeds even before the first map, but the engine resets the event manager's
     * listener table during map startup - so registrations made at plugin load (or on a previous
     * map) are silently dropped and must be re-attached each map. CS2Fixes registers from
     * StartupServer for the same reason.
     */
    void OnServerStartup();

    /**
     * @brief The engine-side listener object the game keeps for @p slot's client.
     *
     * The client's own subscription handle, not a kit listener: firing an event at it delivers to
     * that one client (how @ref MessageSystem sends center HTML), and it is what
     * @ref ClientListensTo interrogates. nullptr when the slot has no client or the
     * "LegacyGameEventListener" gamedata signature did not resolve.
     */
    IGameEventListener2* GetClientLegacyListener(int slot) const;

    /**
     * @brief Whether @p slot's client is subscribed to @p eventName engine-side.
     *
     * A vanilla client subscribes only to events its HUD needs, so subscriptions it has no
     * business holding are a fingerprint of injected client code.
     */
    bool ClientListensTo(int slot, const char* eventName) const;

    void FireGameEvent(IGameEvent* event) override;

private:
    struct RegisteredListener
    {
        std::string EventName;
        EventCallback Callback;
    };

    using GetLegacyGameEventListenerFn = IGameEventListener2* (*)(CPlayerSlot slot);

    Core::CallbackRegistry<RegisteredListener> _listeners;
    std::set<std::string> _registeredEvents;  // every event name ever listened to; see OnServerStartup
    GetLegacyGameEventListenerFn _getLegacyListener = nullptr;
};

}  // namespace CS2Kit::Sdk
