#include "Ui/ButtonPressHook.hpp"

#include "Engine/Net/ServerSideClients.hpp"
#include "Ui/ButtonPressMessage.hpp"

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Unsafe/Hook.hpp>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <string_view>
#include <utility>

namespace VoltMod
{

// The SDK does not define CS_UM_CustomHudClicked, so decode it as a generic user message.
static constexpr std::string_view UserMessageName = "CSVCMsg_UserMessage";
static constexpr int32_t CustomHudClickType = 390;

ButtonPressHook::ButtonPressHook(Interfaces& interfaces, const Bindings& bindings, Scheduler& scheduler,
                                 Event<const ButtonPress&>& pressed)
    : _interfaces(interfaces), _bindings(bindings), _scheduler(scheduler), _pressed(pressed)
{}

ButtonPressHook::~ButtonPressHook()
{
    // A remaining hook may call into an unloaded module after Runtime is destroyed.
    if (_hook)
        Log::Error("ButtonPressHook: a press subscription outlived the hook; a handler may dangle.");
}

bool ButtonPressHook::Install()
{
    if (!_bindings.ClientMessageFilter)
    {
        Log::Warn("ButtonPressHook: the FilterMessage client offset did not bind; button presses will not arrive.");
        return false;
    }
    if (auto* message = _interfaces.NetworkMessages
                            ? _interfaces.NetworkMessages->FindNetworkMessagePartial(std::string(UserMessageName).c_str())
                            : nullptr)
        _messageId = message->GetNetMessageInfo()->m_MessageId;
    if (_messageId < 0)
    {
        Log::Warn("ButtonPressHook: the engine does not know {}; button presses will not arrive.", UserMessageName);
        return false;
    }

    // Hook the filter's vtable slot. Every plugin must locate the same unpatched table.
    auto hook =
        HookVirtual("Custom HUD button presses", _bindings.FilterMessage,
                    [this](EngineMessageFilter& filter, const CNetMessage* message, void*) { Queue(message, filter); });
    if (!hook)
    {
        Log::Warn("ButtonPressHook: {}; button presses will not arrive.", hook.error().Detail);
        return false;
    }

    _hook = std::move(*hook);
    _onFrame = _scheduler.EveryFrame([this] { RaiseQueued(); });
    Log::Info("ButtonPressHook: listening for user message {}, press type {}.", _messageId, CustomHudClickType);
    return true;
}

void ButtonPressHook::Remove()
{
    _hook.Reset();
    _onFrame.Reset();
    _queued.clear();
    _messageId = -1;
}

const ButtonPressHook::MessageFields& ButtonPressHook::FieldsOf(const ProtoMessage& proto)
{
    static const MessageFields fields = [&proto] {
        const MessageFields resolved{.Type = ProtoField(proto, "msg_type"), .Data = ProtoField(proto, "msg_data")};
        if (!resolved)
            Log::Warn("ButtonPressHook: {} has no 'msg_type'/'msg_data' field; ignoring presses.", UserMessageName);
        return resolved;
    }();
    return fields;
}

void ButtonPressHook::Queue(const CNetMessage* message, const EngineMessageFilter& filter)
{
    // Filter by message id before parsing the inbound message.
    INetworkMessageInternal* info = message ? message->GetNetMessage() : nullptr;
    if (!info || info->GetNetMessageInfo()->m_MessageId != _messageId)
        return;

    const ProtoMessage* proto = message->ToPB<ProtoMessage>();
    if (!proto)
        return;

    const MessageFields& fields = FieldsOf(*proto);
    if (!fields)
        return;

    const auto* reflection = proto->GetReflection();
    if (reflection->GetInt32(*proto, fields.Type) != CustomHudClickType)
        return;

    // Use the sending connection's slot so a spectator's press remains theirs.
    const int slot = SlotOfClient(_bindings, ClientOfFilter(_bindings, filter));
    if (!IsValidSlot(slot))
        return;

    auto payload = ButtonPressMessage::Parse(reflection->GetString(*proto, fields.Data));
    if (!payload)
    {
        Log::Warn("ButtonPressHook: a press from slot {} did not parse ({}).", slot, payload.error().Detail);
        return;
    }

    // Reject embedded NULs in client-controlled text before formatting it.
    if (payload->ButtonId.find('\0') != std::string::npos)
        return;

    _queued.push_back({.Slot = slot, .ButtonId = std::move(payload->ButtonId)});
}

void ButtonPressHook::RaiseQueued()
{
    // Swap the queue first so handlers may remove the hook and clear it.
    std::vector<ButtonPress> presses;
    presses.swap(_queued);

    for (const ButtonPress& press : presses)
        _pressed.Raise(press);
}

}  // namespace VoltMod
