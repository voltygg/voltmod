#include "Engine/NetMessage.hpp"
#include "Engine/SigScanner.hpp"

#include <igameevents.h>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/MemoryAccess.hpp>
#include <VoltMod/Engine/RecipientFilter.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

namespace VoltMod
{

// TextMsg destination ids the client understands.
static constexpr int DestChat = 3;
static constexpr int DestCenter = 4;
static constexpr int DestAlert = 6;

// CS2 strips leading color escapes until a non-color byte. Prepend a space to
// preserve a leading color, or Default to avoid color carryover from the prior line.
static std::string EnsureColorPrefix(std::string_view message)
{
    std::string_view prefix =
        (!message.empty() && static_cast<unsigned char>(message.front()) <= 0x10)  // 0x01-0x10: color escape bytes
            ? " "
            : ChatColors::Default;

    std::string out;
    out.reserve(prefix.size() + message.size());
    out.append(prefix);
    out.append(message);
    return out;
}

static std::string Render(std::string_view message, MessageKind kind)
{
    return kind == MessageKind::Chat ? EnsureColorPrefix(message) : std::string(message);
}

Messages::Messages(Interfaces& interfaces, const Bindings& bindings, GameEvents& events, Translations& translations)
    : _interfaces(interfaces), _bindings(bindings), _events(events), _translations(translations)
{}

Status Messages::Initialize()
{
    auto& interfaces = _interfaces;

    if (!interfaces.GameEventSystem)
        return std::unexpected(Error::NotReady("IGameEventSystem not available"));

    if (!interfaces.NetworkMessages)
        return std::unexpected(Error::NotReady("INetworkMessages not available"));

    Log::Info("Message system initialized.");
    return {};
}

Status Messages::InitGameEventManager()
{
    auto& interfaces = _interfaces;

    if (!_bindings.GameEventManager)
        return std::unexpected(Error::Unsupported("the GameEventManager address did not bind"));

    interfaces.GameEventManager = ReadAt<IGameEventManager2*>(_bindings.GameEventManager.Ptr(), 0);
    if (!interfaces.GameEventManager)
        return std::unexpected(Error::Engine("the game event manager pointer is null"));

    Log::Info("Game event manager resolved at {:#x}.", reinterpret_cast<uintptr_t>(interfaces.GameEventManager));
    return {};
}

void Messages::SendCenterHtml(int slot, const std::string& html)
{
    auto* gameEventManager = _interfaces.GameEventManager;
    if (!gameEventManager || !IsValidSlot(slot))
        return;

    IGameEvent* pEvent = gameEventManager->CreateEvent("show_survival_respawn_status");
    if (!pEvent)
        return;

    pEvent->SetString("loc_token", html.c_str());
    pEvent->SetInt("userid", slot);
    pEvent->SetInt("duration", 5);

    // Deliver to just this client when the engine exposes its listener. Otherwise the event
    // broadcasts and every client renders the panel.
    if (IGameEventListener2* listener = _events.GetClientLegacyListener(slot))
    {
        listener->FireGameEvent(pEvent);
        gameEventManager->FreeEvent(pEvent);
        return;
    }

    gameEventManager->FireEvent(pEvent);
}

void Messages::Send(int slot, std::string_view message, MessageKind kind)
{
    if (kind == MessageKind::CenterHtml)
    {
        SendCenterHtml(slot, std::string(message));
        return;
    }

    int destination = kind == MessageKind::Center ? DestCenter : kind == MessageKind::Alert ? DestAlert : DestChat;
    SendTextMsg(slot, destination, Render(message, kind));
}

void Messages::Broadcast(std::string_view message, MessageKind kind)
{
    auto rendered = Render(message, kind);

    if (kind == MessageKind::CenterHtml)
    {
        // Per-slot, because each panel write targets one client's own event listener.
        // A null listener means nobody is in that slot.
        for (int slot = 0; slot < MaxPlayers; ++slot)
            if (_events.GetClientLegacyListener(slot))
                SendCenterHtml(slot, rendered);
        return;
    }

    // One event for everyone rather than one per player: the engine drops slots with no
    // client from the recipient bits, which is also how this avoids needing the roster.
    MultiRecipientFilter filter;
    for (int slot = 0; slot < MaxPlayers; ++slot)
        filter.AddRecipient(slot);

    PostTextMsg(filter,
                kind == MessageKind::Center  ? DestCenter
                : kind == MessageKind::Alert ? DestAlert
                                             : DestChat,
                rendered);
}

void Messages::Reply(int slot, std::string_view message)
{
    Send(slot, message);
}

void Messages::ReplyKey(int slot, const std::string& key, const std::map<std::string, std::string>& tokens)
{
    Reply(slot, _translations.Get(key, slot, tokens));
}

void Messages::SendTextMsg(int slot, int destination, const std::string& message)
{
    if (!IsValidSlot(slot))
        return;

    SingleRecipientFilter filter(slot);
    PostTextMsg(filter, destination, message);
}

void Messages::PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message)
{
    // CS2 routes server-originated chat through TextMsg with dest=HUD_PRINTTALK rather than
    // SayText2. SayText2 requires a real source player and silently drops messages whose
    // entityindex doesn't resolve to a connected client.
    if (!_textMsgInternal && _interfaces.NetworkMessages)
        _textMsgInternal = _interfaces.NetworkMessages->FindNetworkMessage("CUserMessageTextMsg");

    PostUserMessage(_interfaces, _textMsgInternal, "TextMsg", filter, [&](CNetMessage* raw) {
        auto* textMsg = raw->ToPB<CUserMessageTextMsg>();
        if (!textMsg)
            return false;
        textMsg->set_dest(destination);
        textMsg->add_param(message.c_str());
        return true;
    });
}

void Messages::Shake(int slot, float durationSec, float frequency, float amplitude)
{
    SingleRecipientFilter filter(slot);
    PostUserMessage(_interfaces, _shakeInternal, "Shake", filter, [&](CNetMessage* raw) {
        auto* shake = raw->ToPB<CUserMessageShake>();
        if (!shake)
            return false;
        shake->set_duration(durationSec);
        shake->set_frequency(frequency);
        shake->set_amplitude(amplitude);
        shake->set_command(0);  // SHAKE_START
        return true;
    });
}

void Messages::ClearCenterHtml(int slot)
{
    SendCenterHtml(slot, " ");
}

}  // namespace VoltMod
