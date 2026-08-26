#include "App.hpp"

#include <VoltMod/Api.hpp>

// Every framework name lives in VoltMod. Name the few a file leans on here, in the .cpp -
// never a using-directive, and never in a header.
using VoltMod::CommandContext;

namespace $ns
{

// Registered explicitly from Start, so every handler is handed what it needs
// instead of reaching for a global. Add more here or in new .cpp files.
void RegisterCommands(VoltMod::CommandManager& commands)
{
    commands.Register({
        .Name = "ping",
        .Description = "Check that the plugin is alive.",
        .Usage = "!ping",
        .Handler = [](CommandContext& c) { return c.Ok("cmd.pong"); },
    });
}

}  // namespace $ns
