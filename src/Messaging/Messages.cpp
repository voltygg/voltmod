#include "Engine/NetMessage.hpp"
#include "Engine/SigScanner.hpp"

#include <igameevents.h>

#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Slot.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/GameData.hpp>
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

namespace VoltMod::Messaging
{

using namespace VoltMod::Core;

namespace
{

// TextMsg destination ids the client understands.
constexpr int DestChat = 3;
constexpr int DestCenter = 4;
constexpr int DestAlert = 6;

// CS2 strips leading color escapes until a non-color byte. Prepend a space to
// preserve a leading color, or Default to avoid color carryover from the prior line.
std::string EnsureColorPrefix(std::string_view message)
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

std::string Render(std::string_view message, MessageKind kind)
{
    return kind == MessageKind::Chat ? EnsureColorPrefix(message) : std::string(message);
}

}  // namespace

Messages::Messages(Engine::Interfaces& interfaces, Engine::GameData& gameData, Events::GameEvents& events,
                   Core::Translations& translations)
    : _interfaces(interfaces), _gameData(gameData), _events(events), _translations(translations)
{}

bool Messages::Initialize()
{
    auto& interfaces = _interfaces;

    if (!interfaces.GameEventSystem)
    {
        Log::Error("IGameEventSystem not available.");
        return false;
    }

    if (!interfaces.NetworkMessages)
    {
        Log::Error("INetworkMessages not available.");
        return false;
    }

    Log::Info("Message system initialized.");
    return true;
}

bool Messages::InitGameEventManager()
{
    auto& interfaces = _interfaces;

    void* eventManagerAddr = _gameData.ResolveSignature("GameEventManager");
    if (eventManagerAddr)
    {
        interfaces.GameEventManager = Engine::ReadAt<IGameEventManager2*>(eventManagerAddr, 0);

        if (interfaces.GameEventManager)
        {
            Log::Info("Game event manager resolved at {:#x}.",
                      reinterpret_cast<uintptr_t>(interfaces.GameEventManager));
        }
        else
        {
            Log::Warn("Game event manager pointer is null after resolve.");
        }
    }
    else
    {
        Log::Warn("GameEventManager signature not found.");
    }

    return interfaces.GameEventManager != nullptr;
}

void Messages::SendCenterHtml(int slot, const std::string& html)
{
    auto* gameEventManager = _interfaces.GameEventManager;
    if (!gameEventManager || !Core::IsValidSlot(slot))
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
        for (int slot = 0; slot < Core::MaxPlayers; ++slot)
            if (_events.GetClientLegacyListener(slot))
                SendCenterHtml(slot, rendered);
        return;
    }

    // One event for everyone rather than one per player: the engine drops slots with no
    // client from the recipient bits, which is also how this avoids needing the roster.
    Engine::MultiRecipientFilter filter;
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
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
    if (!Core::IsValidSlot(slot))
        return;

    Engine::SingleRecipientFilter filter(slot);
    PostTextMsg(filter, destination, message);
}

void Messages::PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message)
{
    // CS2 routes server-originated chat through TextMsg with dest=HUD_PRINTTALK rather than
    // SayText2. SayText2 requires a real source player and silently drops messages whose
    // entityindex doesn't resolve to a connected client.
    if (!_textMsgInternal && _interfaces.NetworkMessages)
        _textMsgInternal = _interfaces.NetworkMessages->FindNetworkMessage("CUserMessageTextMsg");

    Engine::PostUserMessage(_interfaces, _textMsgInternal, "TextMsg", filter, [&](CNetMessage* raw) {
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
    Engine::SingleRecipientFilter filter(slot);
    Engine::PostUserMessage(_interfaces, _shakeInternal, "Shake", filter, [&](CNetMessage* raw) {
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

}  // namespace VoltMod::Messaging
