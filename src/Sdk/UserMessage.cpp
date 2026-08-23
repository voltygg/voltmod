#include "Sdk/SigScanner.hpp"

#include <igameevents.h>

#include <CS2Kit/Core/Slot.hpp>
#include <CS2Kit/Detail/Runtime.hpp>
#include <CS2Kit/Runtime.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/MemoryAccess.hpp>
#include <CS2Kit/Sdk/RecipientFilter.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <CS2Kit/Utils/ChatColors.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <engine/igameeventsystem.h>
#include <networksystem/inetworkmessages.h>
#include <networksystem/netmessage.h>
#include <usermessages.pb.h>

namespace CS2Kit::Sdk
{

using namespace CS2Kit::Utils;

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

bool MessageSystem::Initialize()
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;

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

bool MessageSystem::InitGameEventManager()
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;
    auto& gameData = CS2Kit::Detail::Rt().GameData;

    void* eventManagerAddr = gameData.ResolveSignature("GameEventManager");
    if (eventManagerAddr)
    {
        interfaces.GameEventManager = ReadAt<IGameEventManager2*>(eventManagerAddr, 0);

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

void MessageSystem::SendCenterHtml(int slot, const std::string& html)
{
    auto* gameEventManager = CS2Kit::Detail::Rt().Interfaces.GameEventManager;
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
    if (IGameEventListener2* listener = CS2Kit::Detail::Rt().Events.GetClientLegacyListener(slot))
    {
        listener->FireGameEvent(pEvent);
        gameEventManager->FreeEvent(pEvent);
        return;
    }

    gameEventManager->FireEvent(pEvent);
}

void MessageSystem::Send(int slot, std::string_view message, MessageKind kind)
{
    if (kind == MessageKind::CenterHtml)
    {
        SendCenterHtml(slot, std::string(message));
        return;
    }

    int destination = kind == MessageKind::Center ? DestCenter : kind == MessageKind::Alert ? DestAlert : DestChat;
    SendTextMsg(slot, destination, Render(message, kind));
}

void MessageSystem::Broadcast(std::string_view message, MessageKind kind)
{
    auto rendered = Render(message, kind);

    if (kind == MessageKind::CenterHtml)
    {
        // Per-slot, because each panel write targets one client's own event listener.
        // A null listener means nobody is in that slot.
        for (int slot = 0; slot < Core::MaxPlayers; ++slot)
            if (CS2Kit::Detail::Rt().Events.GetClientLegacyListener(slot))
                SendCenterHtml(slot, rendered);
        return;
    }

    // One event for everyone rather than one per player: the engine drops slots with no
    // client from the recipient bits, which is also how this avoids needing the roster.
    MultiRecipientFilter filter;
    for (int slot = 0; slot < Core::MaxPlayers; ++slot)
        filter.AddRecipient(slot);

    PostTextMsg(filter,
                kind == MessageKind::Center  ? DestCenter
                : kind == MessageKind::Alert ? DestAlert
                                             : DestChat,
                rendered);
}

void MessageSystem::Reply(int slot, std::string_view message)
{
    Send(slot, message);
}

void MessageSystem::ReplyKey(int slot, const std::string& key, const std::map<std::string, std::string>& tokens)
{
    Reply(slot, CS2Kit::Detail::Rt().Translations.Get(key, slot, tokens));
}

void MessageSystem::SendTextMsg(int slot, int destination, const std::string& message)
{
    if (!Core::IsValidSlot(slot))
        return;

    SingleRecipientFilter filter(slot);
    PostTextMsg(filter, destination, message);
}

void MessageSystem::PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message)
{
    auto& interfaces = CS2Kit::Detail::Rt().Interfaces;
    if (!interfaces.GameEventSystem || !interfaces.NetworkMessages)
        return;

    // CS2 routes server-originated chat through TextMsg with dest=HUD_PRINTTALK rather than
    // SayText2. SayText2 requires a real source player and silently drops messages whose
    // entityindex doesn't resolve to a connected client.
    if (!_textMsgInternal)
    {
        _textMsgInternal = interfaces.NetworkMessages->FindNetworkMessage("CUserMessageTextMsg");
        if (!_textMsgInternal)
            _textMsgInternal = interfaces.NetworkMessages->FindNetworkMessagePartial("TextMsg");
    }

    if (!_textMsgInternal)
        return;

    CNetMessage* pMsg = _textMsgInternal->AllocateMessage();
    if (!pMsg)
        return;

    auto* pTextMsg = pMsg->ToPB<CUserMessageTextMsg>();
    if (!pTextMsg)
    {
        interfaces.NetworkMessages->DeallocateNetMessageAbstract(_textMsgInternal, pMsg);
        return;
    }
    pTextMsg->set_dest(destination);
    pTextMsg->add_param(message.c_str());

    interfaces.GameEventSystem->PostEventAbstract(-1, false, &filter, _textMsgInternal, pMsg, 0);

    interfaces.NetworkMessages->DeallocateNetMessageAbstract(_textMsgInternal, pMsg);
}

void MessageSystem::ClearCenterHtml(int slot)
{
    SendCenterHtml(slot, " ");
}

}  // namespace CS2Kit::Sdk
