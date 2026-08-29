#pragma once

#include <igameevents.h>

#include <VoltMod/Core/CallbackRegistry.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Wrapper for IGameEventManager2 providing event creation, firing, and listener registration.
 */
class GameEvents : public IGameEventListener2
{
public:
    /** Both must outlive this service, which detaches from the engine in its destructor. */
    GameEvents(Interfaces& interfaces, const Bindings& bindings);
    ~GameEvents() override;
    GameEvents(const GameEvents&) = delete;
    GameEvents& operator=(const GameEvents&) = delete;

    /** Attach to IGameEventManager2. Error::NotReady when Messages did not resolve it. */
    Status Initialize();

    IGameEvent* CreateEvent(std::string_view name);
    bool FireEvent(IGameEvent* event, bool dontBroadcast = false);
    void FreeEvent(IGameEvent* event);

    /**
     * Subscribe to one game event for as long as the returned Subscription lives.
     *
     * @p TEvent is a struct from `<VoltMod/Events/EventTypes.hpp>` carrying `Name` and `From`.
     * There is no string form: an event nobody has modeled is one nobody decodes consistently,
     * so consuming a new one means adding its struct there first.
     */
    template <class TEvent>
    [[nodiscard]] Subscription On(std::function<void(const TEvent&)> handler)
    {
        return Add(TEvent::Name, [h = std::move(handler)](IGameEvent* e) {
            if (e)
                h(TEvent::From(*e));
        });
    }

    /** @brief Remove all listeners and deregister from the engine. */
    void RemoveAllListeners();

    /**
     * @brief Re-attach every listener after map startup.
     *
     * The engine resets the listener table during map startup, including registrations made at
     * plugin load or on a previous map.
     */
    void OnServerStartup();

    /**
     * @brief The engine-side listener object the game keeps for @p slot's client.
     *
     * The client's own subscription handle, not a framework listener: firing an event at it delivers to
     * that one client (how @ref Messages sends center HTML), and it is what
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
    bool ClientListensTo(int slot, std::string_view eventName) const;

    void FireGameEvent(IGameEvent* event) override;

private:
    using EventCallback = std::function<void(IGameEvent*)>;

    /** Store one raw-IGameEvent listener under @p eventName; @ref On is the only caller. */
    [[nodiscard]] Subscription Add(std::string_view eventName, EventCallback callback);

    struct RegisteredListener
    {
        std::string EventName;
        EventCallback Callback;
    };

    using GetLegacyGameEventListenerFn = IGameEventListener2* (*)(CPlayerSlot slot);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    CallbackRegistry<RegisteredListener> _listeners;
    std::set<std::string> _registeredEvents;  // every event name ever listened to; see OnServerStartup
    GetLegacyGameEventListenerFn _getLegacyListener = nullptr;
};

}  // namespace VoltMod
