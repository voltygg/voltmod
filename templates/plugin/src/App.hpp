#pragma once

#include "Config.hpp"

#include <CS2Kit/Api.hpp>

namespace $ns
{

/**
 * Everything this plugin owns for one Load/Unload cycle. The plugin creates it in OnLoad
 * and drops it in OnUnload, so no state survives a `meta reload`.
 *
 * Members are declared in dependency order and destroyed in reverse; each takes the
 * collaborators it needs, so nothing here reaches for a global.
 */
struct App
{
    explicit App(CS2Kit::Runtime& runtime) : Runtime(runtime) {}

    /** Load config and register commands. False aborts the plugin load. */
    bool Start();

    CS2Kit::Runtime& Runtime;
    ConfigManager Config;
};

}  // namespace $ns
