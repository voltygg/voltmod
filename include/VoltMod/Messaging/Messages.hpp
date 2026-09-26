#pragma once

#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Events/GameEvents.hpp>
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
 * Chat sends preserve a leading color escape or prepend the default color. `Send` and `Broadcast`
 * send finished text; `SendKey` and `BroadcastKey` translate a key into each player's language.
 */
class Messages
{
public:
    /** @p interfaces, @p events, and @p translations must outlive this service. */
    Messages(Interfaces& interfaces, GameEvents& events, Translations& translations);
    Messages(const Messages&) = delete;
    Messages& operator=(const Messages&) = delete;

    /** NotReady when the engine's event or network message system is missing. */
    Status Available() const;

    void Send(int slot, std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Broadcast to connected human players. */
    void Broadcast(std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Translate @p key into @p slot's language, substitute @p tokens, and send it. */
    void SendKey(int slot, std::string_view key, const Tokens& tokens = {}, MessageKind kind = MessageKind::Chat);

    /** `SendKey` to every connected client, each in their own language. */
    void BroadcastKey(std::string_view key, const Tokens& tokens = {}, MessageKind kind = MessageKind::Chat);

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
