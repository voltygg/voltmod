#include "Engine/Net/NetMessage.hpp"
#include "Engine/Memory/SigScanner.hpp"

#include <igameevents.h>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Net/RecipientFilter.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Messaging/ChatColors.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

namespace VoltMod
{

// TextMsg destination ids supported by the client.
static constexpr int DestChat = 3;
static constexpr int DestCenter = 4;
static constexpr int DestAlert = 6;

// Prepend a space to preserve a leading color, or Default to prevent color carryover.
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

Messages::Messages(Interfaces& interfaces, GameEvents& events, Translations& translations)
    : _interfaces(interfaces), _events(events), _translations(translations)
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

    // Use the client's listener when available; otherwise the event broadcasts to every client.
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
        // Each panel write targets one client's listener; null means the slot is empty.
        for (int slot = 0; slot < MaxPlayers; ++slot)
            if (_events.GetClientLegacyListener(slot))
                SendCenterHtml(slot, rendered);
        return;
    }

    // One event lets the engine remove empty slots from recipient bits without a roster scan.
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
    // Server chat uses TextMsg with HUD_PRINTTALK; SayText2 requires a connected source player.
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
