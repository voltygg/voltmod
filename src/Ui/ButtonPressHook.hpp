#pragma once

#include "Engine/Net/ProtoReflect.hpp"

#include <VoltMod/Core/Event.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/Subscription.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <vector>

namespace VoltMod
{

/**
 * @brief The `FilterMessage` hook button presses come back through.
 *
 * Owned by @ref ScreenManager, whose Pressed event installs it only while something listens.
 * Presses are raised on the next game frame. Inert when @ref Capability::ButtonPresses is off.
 */
class ButtonPressHook
{
public:
    /** All references must outlive this hook. */
    ButtonPressHook(Interfaces& interfaces, const Bindings& bindings, Scheduler& scheduler,
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

    static const MessageFields& FieldsOf(const ProtoMessage& proto);

    /** Queue a press for the next frame; never changes the engine's verdict. */
    void Queue(const CNetMessage* message, const EngineMessageFilter& filter);
    void RaiseQueued();

    Interfaces& _interfaces;
    const Bindings& _bindings;
    Scheduler& _scheduler;
    Event<const ButtonPress&>& _pressed;  ///< owned by ScreenManager

    int _messageId = -1;
    std::vector<ButtonPress> _queued;
    Subscription _onFrame;
    Subscription _hook;
};

}  // namespace VoltMod
