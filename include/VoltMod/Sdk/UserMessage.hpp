#pragma once

#include <map>
#include <string>
#include <string_view>

class INetworkMessageInternal;

namespace VoltMod::Sdk
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
class MessageSystem
{
public:
    MessageSystem() = default;

    bool Initialize();
    bool InitGameEventManager();

    /** Send one message to one player. */
    void Send(int slot, std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Send to every connected player (bots and empty slots skipped). */
    void Broadcast(std::string_view message, MessageKind kind = MessageKind::Chat);

    /** Chat reply to a command caller; shorthand for `Send(slot, message)`. */
    void Reply(int slot, std::string_view message);

    /** Translate @p key for the player's language, substitute @p tokens, and Reply. */
    void ReplyKey(int slot, const std::string& key, const std::map<std::string, std::string>& tokens = {});

    /** Raw center-HTML panel write; menus and @ref PersistentCenterHtml build on this. */
    void SendCenterHtml(int slot, const std::string& html);
    void ClearCenterHtml(int slot);

private:
    void SendTextMsg(int slot, int destination, const std::string& message);
    /** Post one already-rendered TextMsg to whoever @p filter selects. */
    void PostTextMsg(IRecipientFilter& filter, int destination, const std::string& message);

    INetworkMessageInternal* _textMsgInternal = nullptr;
};

}  // namespace VoltMod::Sdk
