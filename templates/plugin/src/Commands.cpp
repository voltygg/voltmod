#include "App.hpp"

#include <VoltMod/Api.hpp>
#include <vector>

// Every framework name lives in VoltMod. Name the few a file leans on here, in the .cpp -
// never a using-directive, and never in a header.
using VoltMod::Caller;
using VoltMod::Reply;
using VoltMod::Result;
using VoltMod::Subscription;

// The command argument types are the one nested namespace worth an alias.
namespace Args = VoltMod::Args;

namespace $ns
{

// Registered explicitly from Start, so every handler is handed what it needs
// instead of reaching for a global. Add more here or in new .cpp files.
//
// The handler's parameter list is the argument spec: adding `Args::Target t`
// after the Caller declares one target argument, parses it, checks immunity, and
// hands the handler a resolved player. Each Run() returns the registration -
// keep it beside the state its handler captured.
void RegisterCommands(VoltMod::CommandManager& commands, std::vector<Subscription>& subs)
{
    subs.push_back(commands.Add("ping")
                       .Describe("Check that the plugin is alive.")
                       .Run([](Caller c) -> Result<Reply> { return c.Ok("cmd.pong"); }));
}

}  // namespace $ns
