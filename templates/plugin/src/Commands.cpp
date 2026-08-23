#include "App.hpp"

#include <CS2Kit/Api.hpp>

using namespace CS2Kit::Commands;

namespace $ns
{

// Registered explicitly from App::Start, so every handler is handed what it needs
// instead of reaching for a global. Add more here or in new .cpp files.
void RegisterCommands(CS2Kit::CommandManager& commands)
{
    commands.Register({
        .Name = "ping",
        .Description = "Check that the plugin is alive.",
        .Usage = "!ping",
        .Handler = [](CommandContext& c) { return c.Ok("cmd.pong"); },
    });
}

}  // namespace $ns
