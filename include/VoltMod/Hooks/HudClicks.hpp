#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <string>

namespace VoltMod
{

/** A `Button` press inside a custom HUD layout. */
struct HudClick
{
    int Slot = -1;         ///< who clicked
    EntityRef Layout;      ///< the custom_hud_layout the Button belongs to
    std::string ButtonId;  ///< the Button's `id` attribute
};

/**
 * @brief Button presses coming back from a custom HUD layout.
 *
 * The client sends `CCSUsrMsg_CustomHudClicked` when a `Button` in a layout is pressed, and this
 * turns it into @ref Clicked. A press only happens at all once that player has a cursor, which is
 * `HudLayout::SetInputCapture`; without it the game keeps mouse-look and the panel never sees a
 * pointer.
 *
 * A `Button` with no `id` attribute is dropped by the client before it is ever sent, so an
 * unnamed button silently does nothing.
 *
 * ### Arming
 *
 * Dormant until something subscribes, and the hook is removed when the last subscription drops -
 * there is no separate install step. Unlike the framework's other vtable hooks this one cannot
 * bind from a cold start: `CServerSideClient::FilterMessage` is inherited from a secondary base,
 * so it is not in the class's primary vtable and the slot is located from a live client instead
 * (see FindVTableSlot). Subscribing with nobody connected therefore arms on the next connect;
 * that is handled here, and callers see no difference beyond clicks not arriving from an empty
 * server.
 *
 * Inert when @ref Capability::HudClicks is off. Every handler runs on the game thread.
 */
class HudClicks
{
public:
    /** All four must outlive this hook; the Runtime declares them above it. */
    HudClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots);
    ~HudClicks();
    HudClicks(const HudClicks&) = delete;
    HudClicks& operator=(const HudClicks&) = delete;

    /** A player pressed a Button. Subscribing installs the hook; see the class docs. */
    Event<const HudClick&> Clicked;

private:
    /** Install on the first subscription, remove on the last. */
    bool Acquire();
    void ReleaseRef();

    /** Bind the secondary vtable from a connected client. False while nobody is connected, which
     *  is retried from the slot listener rather than treated as a failure. */
    bool Install();

    bool Hook_FilterMessage(const CNetMessage* message, void* channel);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    SlotEvents& _slots;

    int _refs = 0;                 // live subscriptions
    int _baseOffset = 0;           // bytes from CServerSideClient to the hooked subobject
    int _messageId = -1;           // CCSUsrMsg_CustomHudClicked, from the engine's own registry
    Subscription _connectListener;  // retries Install() while subscribed but not yet armed
    VtableHook _hook;
};

}  // namespace VoltMod
