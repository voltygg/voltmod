#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <map>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Where a message renders on the client. */
enum class MessageKind
{
    Chat,        ///< Chat box line (color escapes honored).
    Center,      ///< Plain center-screen print.
    CenterHtml,  ///< Center HTML panel (same channel the menu system renders into).
    Alert,       ///< Top-center alert bar.
};

/**
 * @brief The one place messages leave the server: chat, center print, center HTML, alerts.
 *
 * Chat sends normalize colors automatically - a leading color escape is preserved, otherwise
 * the default chat color is prepended so lines don't inherit the previous line's color.
 * `ReplyKey` is the command-reply one-liner: translate for the player's language, substitute
 * tokens, send to their chat.
 */
class Messages
{
public:
    /** All four must outlive this service; the Runtime declares them above it. */
    Messages(Interfaces& interfaces, const Bindings& bindings, GameEvents& events, Translations& translations);
    Messages(const Messages&) = delete;
    Messages& operator=(const Messages&) = delete;

    /** Bind the engine message systems. Error::NotReady when either is unavailable. */
    Status Initialize();

    /** Read IGameEventManager2 out of its gamedata address. Error when it did not resolve. */
    Status InitGameEventManager();

    /** Send one message to one player. */
    void Send(int slot, std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Send to every connected player (bots and empty slots skipped). */
    void Broadcast(std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Chat reply to a command caller; shorthand for `Send(slot, message)`. */
    void Reply(int slot, std::string_view message);

    /** Translate @p key for the player's language, substitute @p tokens, and Reply. */
    void ReplyKey(int slot, const std::string& key, const std::map<std::string, std::string>& tokens = {});

    /**
     * Shake @p slot's screen, the engine's own CUserMessageShake.
     *
     * @param durationSec how long the shake lasts.
     * @param frequency oscillations per second; higher reads as a rattle, lower as a sway.
     * @param amplitude how far the view is thrown.
     */
    void Shake(int slot, float durationSec, float frequency, float amplitude);

    /** Raw center-HTML panel write; menus and @ref CenterHtml build on this. */
    void SendCenterHtml(int slot, const std::string& html);
    void ClearCenterHtml(int slot);

private:
    void SendTextMsg(int slot, int destination, const std::string& message);
    /** Post one already-rendered TextMsg to whoever @p filter selects. */
    void PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message);

    Interfaces& _interfaces;
    const Bindings& _bindings;
    GameEvents& _events;
    Translations& _translations;
    INetworkMessageInternal* _textMsgInternal = nullptr;
    INetworkMessageInternal* _shakeInternal = nullptr;
};

}  // namespace VoltMod
