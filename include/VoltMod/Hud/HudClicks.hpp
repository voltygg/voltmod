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
 * @brief Button presses coming back from a custom HUD layout, unfiltered.
 *
 * Owned by @ref CustomHud and reached as `Hud.Clicks`; @ref Hud::OnClick is the per-layout form
 * most callers want. A press only happens once that player has a cursor - see
 * @ref Hud::SetInputCapture and @ref custom_hud_guide.
 *
 * Dormant until something subscribes, and removed when the last subscription drops. Unlike the
 * framework's other vtable hooks this one cannot bind from a cold start: `FilterMessage` is
 * inherited from a secondary base, so it is not in the class's primary vtable and the slot is
 * located from a live client instead. Subscribing with nobody connected therefore arms on the
 * next connect, which callers see only as clicks not arriving from an empty server.
 *
 * Inert when @ref Capability::HudClicks is off. Every handler runs on the game thread.
 */
class HudClicks
{
public:
    /** All three must outlive this hook; the Runtime declares them above it. */
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

    int _refs = 0;                  // live subscriptions
    int _baseOffset = 0;            // bytes from CServerSideClient to the hooked subobject
    int _messageId = -1;            // CCSUsrMsg_CustomHudClicked, from the engine's own registry
    Subscription _connectListener;  // retries Install() while subscribed but not yet armed
    VtableHook _hook;
};

}  // namespace VoltMod
