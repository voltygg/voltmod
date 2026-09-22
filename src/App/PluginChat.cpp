#include <VoltMod/App/Internal/PluginModule.hpp>
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
    if (Runtime.Hooks.ChatInput.TryConsume(player->Slot(), message))
    {
        return true;
    }

    return Runtime.Commands.HandleChatMessage(player, message);
}

namespace Internal
{

bool PluginModule::HandleConsoleCommand(std::string_view name, std::string_view arguments, int slot)
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
    return player != nullptr && _plugin->OnPlayerChat(player, message, teamChat);
}

}  // namespace Internal
}  // namespace VoltMod
