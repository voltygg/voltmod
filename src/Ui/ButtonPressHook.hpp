#pragma once

#include "Engine/Net/ProtoReflect.hpp"

#include <VoltMod/Core/Signals/Event.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/GameData/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Ui/ButtonPress.hpp>
#include <vector>

namespace VoltMod
{

/**
 * @brief Receives button presses through the `FilterMessage` hook.
 *
 * @ref ScreenManager owns the hook and installs it while `Pressed` has subscribers. Presses are
 * raised on the next game frame. The hook is inactive when its binding is missing.
 */
class ButtonPressHook
{
public:
    /** All referenced services must outlive this hook. */
    ButtonPressHook(Interfaces& interfaces, const Bindings& bindings, Scheduler& scheduler,
                    Event<const ButtonPress&>& pressed);
    ~ButtonPressHook();
    ButtonPressHook(const ButtonPressHook&) = delete;
    ButtonPressHook& operator=(const ButtonPressHook&) = delete;

    /** Install the hook when gamedata and the message registry are available. */
    bool Install();

    /** Unhook after the last subscriber leaves. */
    void Remove();

private:
    /** User-message fields used to decode a press, resolved once per process. */
    struct MessageFields
    {
        const ProtoFieldDescriptor* Type = nullptr;
        const ProtoFieldDescriptor* Data = nullptr;

        explicit operator bool() const { return Type && Data; }
    };

    static const MessageFields& FieldsOf(const ProtoMessage& proto);

    /** Queue a press for the next frame without changing the engine's verdict. */
    void Queue(const CNetMessage* message, const INetworkMessageProcessingPreFilter& filter);
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
