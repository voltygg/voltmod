#include "Managers.hpp"

#include <CS2Kit/Api.hpp>

using namespace CS2Kit::Commands;

// Specs self-register at their definition site; the kit ingests them after OnLoad.
// Add more here or in new .cpp files - there is no central registration list.
static const bool _pingRegistered = CS2Kit::Registry<CS2Kit::CommandSpec>::Add({
    .Name = "ping",
    .Description = "Check that the plugin is alive.",
    .Usage = "!ping",
    .Handler = [](CommandContext& c) { return c.Ok("cmd.pong"); },
});
