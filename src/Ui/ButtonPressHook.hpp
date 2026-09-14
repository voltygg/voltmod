#pragma once

#include "Engine/Net/ProtoReflect.hpp"

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace VoltMod
{

/**
 * @brief The `FilterMessage` hook button presses come back through.
 *
 * Owned by @ref ScreenManager, whose Pressed event installs it only while something listens.
 * Presses are raised on the next game frame. Inert when @ref Capability::UiClicks is off.
 */
class ButtonPressHook
{
public:
    /** All references must outlive this hook. */
    ButtonPressHook(Interfaces& interfaces, const Bindings& bindings, EntitySystem& entities, Scheduler& scheduler,
                    Event<const ButtonPress&>& pressed);
    ~ButtonPressHook();
    ButtonPressHook(const ButtonPressHook&) = delete;
    ButtonPressHook& operator=(const ButtonPressHook&) = delete;

    /** Hook if gamedata and the message registry allow it. False logs why and refuses the subscription. */
    bool Install();

    /** Unhook after the last subscriber leaves. */
    void Remove();

private:
    /** The user-message fields a press is read from, resolved once per process. */
    struct MessageFields
    {
        const ProtoFieldDescriptor* Type = nullptr;
        const ProtoFieldDescriptor* Data = nullptr;

        explicit operator bool() const { return Type && Data; }
    };

    /** A press as it arrived, resolved on delivery. */
    struct QueuedPress
    {
        int Slot;
        uint32_t LayoutHandle;
        std::string ButtonId;
    };

    static const MessageFields& FieldsOf(const ProtoMessage& proto);

    /** Queue a press for the next frame; never changes the engine's verdict. */
    void Queue(const CNetMessage* message, const EngineMessageFilter& filter);
    void RaiseQueued();

    /** The live `custom_hud_layout` matching a 24-bit client handle; a stale or forged one matches none. */
    [[nodiscard]] EntityRef FindLayout(uint32_t networkedHandle) const;

    Interfaces& _interfaces;
    const Bindings& _bindings;
    EntitySystem& _entities;
    Scheduler& _scheduler;
    Event<const ButtonPress&>& _pressed;  ///< owned by ScreenManager

    int _messageId = -1;
    std::vector<QueuedPress> _queued;
    Subscription _onFrame;
    Subscription _hook;
};

}  // namespace VoltMod
