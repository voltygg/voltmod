#pragma once

// Internal to src/: this is what pulls VtableHook.hpp - and the SDK behind it - into a translation
// unit, so no public header includes it. CustomUi holds it by unique_ptr for that reason.

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <VoltMod/Unsafe/VtableHook.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @brief The `FilterMessage` hook button presses come back through.
 *
 * Owned by @ref CustomUi, which is the whole public surface: this raises into the @ref Event it was
 * given, and @ref CustomUi::Clicked's @ref Event::Lifecycle is what calls @ref Install and
 * @ref Remove - so the hook is up exactly while something is listening, panels routing through that
 * event included.
 *
 * The hooked vfunc sits in a secondary vtable that can only be located from a connected client, so
 * installing on an empty server binds on the next connect instead of failing.
 *
 * A press is raised on the game frame after it arrives, not from inside the engine's inbound
 * message processing, so a handler may write to the layout or any other entity.
 *
 * Inert when @ref Capability::UiClicks is off.
 */
class UiClicks
{
public:
    /** All references must outlive this hook; @ref CustomUi holds it below the event and the
     *  Runtime declares the rest above the service. */
    UiClicks(Interfaces& interfaces, const Bindings& bindings, SlotEvents& slots, EntitySystem& entities,
             Scheduler& scheduler, Event<const UiClick&>& clicked);
    ~UiClicks();
    UiClicks(const UiClicks&) = delete;
    UiClicks& operator=(const UiClicks&) = delete;

    /** Check the gamedata and the message registry, then hook. False refuses the subscription that
     *  asked, after saying why. */
    bool Install();

    /** Take the hook back down: the last subscriber has gone. */
    void Remove();

private:
    /** Hook the secondary vtable of a connected client. False while nobody is connected, which is
     *  retried from the slot listener rather than treated as a failure. */
    bool HookClient();

    bool Hook_FilterMessage(const CNetMessage* message, void* channel);

    /** The hook's actual work, so the hook itself is one unconditional MRES_IGNORED. @p self is
     *  the hooked subobject, @ref _baseOffset bytes into the client. Only queues. */
    void HandleMessage(const CNetMessage* message, void* self);

    /** Raise what arrived since the last frame. */
    void DeliverPending();

    /** A press as it arrived over the network, resolved when it is delivered. */
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
    /** The event presses are raised into; owned by @ref CustomUi, which outlives this. */
    Event<const UiClick&>& _clicked;

    int _baseOffset = 0;            // bytes from CServerSideClient to the hooked subobject
    int _messageId = -1;            // CSVCMsg_UserMessage, from the engine's own registry
    Subscription _connectListener;  // retries HookClient() until a client is there to read it from
    std::vector<Pending> _pending;
    Subscription _onFrame;  // delivers _pending each frame while the hook is up
    VtableHook _hook;
};

}  // namespace VoltMod
