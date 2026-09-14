#pragma once

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Ui/UiClick.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @brief The `FilterMessage` hook button presses come back through.
 *
 * Owned by @ref UiPanels, whose Clicked lifecycle installs it only while something listens. Presses
 * are raised on the next game frame, so handlers may write to entities. Inert when
 * @ref Capability::UiClicks is off.
 */
class UiClickHook
{
public:
    /** All references must outlive this hook. */
    UiClickHook(Interfaces& interfaces, const Bindings& bindings, EntitySystem& entities, Scheduler& scheduler,
                Event<const UiClick&>& clicked);
    ~UiClickHook();
    UiClickHook(const UiClickHook&) = delete;
    UiClickHook& operator=(const UiClickHook&) = delete;

    /** Hook if gamedata and the message registry allow it. False logs why and refuses the subscription. */
    bool Install();

    /** Unhook after the last subscriber leaves. */
    void Remove();

private:
    /** Queue a press for the next frame; never changes the engine's verdict. */
    void QueuePress(const CNetMessage* message, const EngineMessageFilter& filter);

    void RaisePresses();

    /** A press as it arrived, resolved on delivery. */
    struct QueuedPress
    {
        int Slot;
        uint32_t LayoutHandle;
        std::string ButtonId;
    };

    Interfaces& _interfaces;
    const Bindings& _bindings;
    EntitySystem& _entities;
    Scheduler& _scheduler;
    Event<const UiClick&>& _clicked;  ///< owned by UiPanels

    int _messageId = -1;  // CSVCMsg_UserMessage
    std::vector<QueuedPress> _queued;
    Subscription _onFrame;
    Subscription _hook;
};

}  // namespace VoltMod
