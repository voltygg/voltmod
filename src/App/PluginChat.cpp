#include <VoltMod/App/Internal/PluginModule.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <string_view>

namespace VoltMod::Internal
{

bool PluginModule::OnConsoleCommand(std::string_view name, std::string_view arguments, int slot)
{
    // A ballot for a plugin vote never reaches the engine's own vote controller.
    if (name == "vote")
    {
        return _runtime->Hooks.Vote.TryCastBallot(slot, arguments);
    }
    if (name == "callvote")
    {
        return _runtime->Hooks.Vote.InProgress();
    }

    const bool teamChat = name == "say_team";
    if (name != "say" && !teamChat)
    {
        return false;
    }

    std::string_view message = arguments;
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty() || !IsValidSlot(slot))
    {
        return false;
    }

    // A later plugin's command must reach it past this plugin's chat handling, such as admin-chat tagging.
    if (_runtime->Commands.IsForeign(message))
    {
        return false;
    }

    Player* player = _runtime->Players.Get(slot);
    if (!player)
    {
        return false;
    }

    // Menu input takes precedence over command parsing.
    if (_runtime->Hooks.ChatInput.TryConsume(slot, message) || _runtime->Commands.HandleChatMessage(player, message))
    {
        return true;
    }

    ChatMessage chat{.Sender = *player, .Text = message, .TeamOnly = teamChat};
    _runtime->Players.Said.Raise(chat);
    return chat.Blocked;
}

}  // namespace VoltMod::Internal
