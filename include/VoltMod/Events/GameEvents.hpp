#pragma once

#include <igameevents.h>

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Signals/CallbackRegistry.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/Slot.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
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
    /** Reads IGameEventManager2 from its gamedata address into @p interfaces. Dependencies must
     *  outlive this service, which detaches from the engine on destruction. */
    GameEvents(Interfaces& interfaces, const Bindings& bindings);
    ~GameEvents() override;
    GameEvents(const GameEvents&) = delete;
    GameEvents& operator=(const GameEvents&) = delete;

    /** An error when the event manager did not resolve. */
    Status Available() const;

    IGameEvent* CreateEvent(std::string_view name);
    bool FireEvent(IGameEvent* event, bool broadcast = true);
    void FreeEvent(IGameEvent* event);

    /**
     * Subscribe to one game event for as long as the returned Subscription lives.
     *
     * @p TEvent is a struct from `<VoltMod/Events/EventTypes.hpp>`, generated for every game event. An
     * event whose `Slot` (the player it is about) is not a valid slot never reaches @p handler.
     */
    template <class TEvent>
    [[nodiscard]] Subscription On(std::function<void(const TEvent&)> handler)
    {
        return Add(TEvent::EventName, [h = std::move(handler)](IGameEvent* e) {
            if (!e)
            {
                return;
            }
            const TEvent event = TEvent::From(*e);
            // The player an event is about is always a valid slot for its handler.
            if constexpr (requires { event.Slot; })
            {
                if (!IsValidSlot(event.Slot))
                {
                    return;
                }
            }
            h(event);
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
     * @brief Return the engine-side listener object for @p slot's client.
     *
     * This is the client's own subscription handle, not a framework listener. Firing an event at it
     * delivers to that client, and @ref ClientListensTo uses it. Returns nullptr when the slot has
     * no client or the "GetLegacyGameEventListener" gamedata signature did not resolve.
     */
    IGameEventListener2* GetClientLegacyListener(int slot) const;

    /**
     * @brief Whether @p slot's client is subscribed to @p eventName engine-side.
     *
     * A vanilla client subscribes only to events its HUD needs. Unexpected subscriptions can
     * indicate injected client code.
     */
    bool ClientListensTo(int slot, std::string_view eventName) const;

    void FireGameEvent(IGameEvent* event) override;

private:
    using EventCallback = std::function<void(IGameEvent*)>;

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
    std::set<std::string> _registeredEvents;  // Reattached by OnServerStartup.
    GetLegacyGameEventListenerFn _getLegacyListener = nullptr;
};

}  // namespace VoltMod
