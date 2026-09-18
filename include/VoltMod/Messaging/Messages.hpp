#pragma once

#include <VoltMod/Core/Results/Result.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
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
 * @brief Send chat, center print, center HTML, and alert messages.
 *
 * Chat sends preserve a leading color escape or prepend the default color. `ReplyKey` translates
 * a command reply for the player's language, substitutes tokens, and sends it to chat.
 */
class Messages
{
public:
    /** @p interfaces, @p events, and @p translations must outlive this service. */
    Messages(Interfaces& interfaces, GameEvents& events, Translations& translations);
    Messages(const Messages&) = delete;
    Messages& operator=(const Messages&) = delete;

    /** Bind the engine message systems. Returns Error::NotReady when either is unavailable. */
    Status Initialize();

    void Send(int slot, std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Broadcast to connected human players. */
    void Broadcast(std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Chat reply to a command caller; shorthand for `Send(slot, message)`. */
    void Reply(int slot, std::string_view message);

    /** Translate @p key for the player's language, substitute @p tokens, and Reply. */
    void ReplyKey(int slot, const std::string& key, const std::map<std::string, std::string>& tokens = {});

    /**
     * Shake @p slot's screen, the engine's own CUserMessageShake.
     *
     * @param slot Recipient player slot.
     * @param durationSec how long the shake lasts.
     * @param frequency oscillations per second; higher reads as a rattle, lower as a sway.
     * @param amplitude how far the view is thrown.
     */
    void Shake(int slot, float durationSec, float frequency, float amplitude);

    /** Write a raw center-HTML panel. Menus and @ref CenterHtml use this path. */
    void SendCenterHtml(int slot, const std::string& html);
    void ClearCenterHtml(int slot);

private:
    void SendTextMsg(int slot, int destination, const std::string& message);
    void PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message);

    Interfaces& _interfaces;
    GameEvents& _events;
    Translations& _translations;
    INetworkMessageInternal* _textMsgInternal = nullptr;
    INetworkMessageInternal* _shakeInternal = nullptr;
};

}  // namespace VoltMod
