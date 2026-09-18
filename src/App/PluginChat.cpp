#include <VoltMod/App/Plugin.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <string_view>

namespace VoltMod
{

bool Plugin::OnPlayerChat(Player* player, std::string_view message, bool /*teamChat*/)
{
    // Menu input takes precedence over command parsing.
    if (_runtime->Hooks.ChatInput.TryConsume(player->Slot(), message))
        return true;

    return _runtime->Commands.HandleChatMessage(player, message);
}

bool Plugin::HandleConsoleCommand(std::string_view name, std::string_view arguments, int slot)
{
    // A ballot for a plugin vote never reaches the engine's own vote controller.
    if (name == "vote")
        return _runtime->Hooks.Vote.TryCastBallot(slot, arguments);

    const bool teamChat = name == "say_team";
    if (name != "say" && !teamChat)
        return false;

    std::string_view message = arguments;
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty() || !IsValidSlot(slot))
        return false;

    Player* player = _runtime->Players.Get(slot);
    return player != nullptr && OnPlayerChat(player, message, teamChat);
}

}  // namespace VoltMod
