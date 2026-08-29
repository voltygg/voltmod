#include "App.hpp"

#include <VoltMod/Api.hpp>

// Keep targeted aliases in the .cpp; do not add using-directives or header aliases.
using VoltMod::Caller;
using VoltMod::Reply;
using VoltMod::Result;

namespace Args = VoltMod::Args;

namespace $ns
{

// Register commands from Start. The handler parameter list declares parsed and
// immunity-checked arguments, and the manager owns each registration.
void RegisterCommands(VoltMod::CommandManager& commands)
{
    commands.Add("ping")
                .Describe("Check that the plugin is alive.")
                .Run([](Caller c) -> Result<Reply> { return c.Ok("cmd.pong"); });
}

}  // namespace $ns
