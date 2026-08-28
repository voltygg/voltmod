#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntityRef.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/** A `Button` press inside a custom HUD layout. */
struct UiClick
{
    int Slot = -1;         ///< who clicked
    EntityRef Layout;      ///< the custom_hud_layout the Button belongs to, already resolved
    std::string ButtonId;  ///< the Button's `id` attribute; client-controlled text
};

/**
 * @brief Button presses coming back from custom HUD layouts, unfiltered.
 *
 * Owned by @ref CustomUi and reached as `Ui.Clicks`; @ref UiPanel::OnClick is the per-layout form
 * most callers want. A press only happens once that player has a cursor - see
 * @ref UiPanel::SetInputCapture.
 *
 * Dormant until something subscribes, removed when the last subscription drops. The hooked vfunc
 * (`FilterMessage`) sits in a secondary vtable that can only be located from a connected client,
 * so subscribing on an empty server arms on the next connect.
 *
 * A press is raised on the game frame after it arrives, not from inside the engine's inbound
 * message processing, so a handler may write to the layout or any other entity.
 *
 * Inert when @ref Capability::UiClicks is off.
 */
class UiClicks
{
public:
    /** All references must outlive this hook; the Runtime declares them above it. */
    UiClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots, EntitySystem& entities,
             Scheduler& scheduler);
    ~UiClicks();
    UiClicks(const UiClicks&) = delete;
    UiClicks& operator=(const UiClicks&) = delete;

    /** A player pressed a Button. Subscribing installs the hook; see the class docs. */
    Event<const UiClick&> Clicked;

private:
    /** First subscription: check the gamedata and the message registry, then @ref Install. */
    bool Acquire();
    /** Last subscription: remove the hook. */
    void ReleaseRef();

    /** Bind the secondary vtable from a connected client. False while nobody is connected, which
     *  is retried from the slot listener rather than treated as a failure. */
    bool Install();

    bool Hook_FilterMessage(const CNetMessage* message, void* channel);

    /** The hook's actual work, so the hook itself is one unconditional MRES_IGNORED. @p self is
     *  the hooked subobject, @ref _baseOffset bytes into the client. Only queues. */
    void HandleMessage(const CNetMessage* message, void* self);

    /** Raise what arrived since the last frame. */
    void Deliver();

    /** A press as it came off the wire, resolved when it is delivered. */
    struct Pending
    {
        int Slot;
        uint32_t Layout;
        std::string Button;
    };

    Interfaces& _interfaces;
    const Bindings& _bindings;
    SlotEvents& _slots;
    EntitySystem& _entities;
    Scheduler& _scheduler;

    int _refs = 0;                  // live subscriptions
    int _baseOffset = 0;            // bytes from CServerSideClient to the hooked subobject
    int _messageId = -1;            // CSVCMsg_UserMessage, from the engine's own registry
    Subscription _connectListener;  // retries Install() while subscribed but not yet armed
    std::vector<Pending> _pending;
    Subscription _pump;  // delivers _pending each frame while the hook is up
    VtableHook _hook;
};

}  // namespace VoltMod
