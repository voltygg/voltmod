#pragma once

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <vector>

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
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}

    /** Load config and register commands. False aborts the plugin load. */
    bool Start();

    VoltMod::Runtime& Runtime;
    ConfigManager Config;

private:
    /** Command and listener registrations, released together. Declared last: reverse member
     *  destruction stops the handlers before the state they capture goes away. */
    std::vector<VoltMod::Subscription> _subs;
};

}  // namespace $ns
