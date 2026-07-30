#include "Managers.hpp"

#include <CS2Kit/Api.hpp>

using namespace CS2Kit::Commands;

// Commands self-register into the Registry at their definition site; the kit ingests
// them automatically after OnLoad. Add more specs here (or in new .cpp files) and they
// are picked up on the next load - no central registration list to maintain.
static const bool _pingRegistered = CS2Kit::Registry<CS2Kit::CommandSpec>::Add({
    .Name = "ping",
    .Description = "Check that the plugin is alive.",
    .Usage = "!ping",
    .Handler = [](CommandContext& c) { return c.Ok("cmd.pong"); },
});
