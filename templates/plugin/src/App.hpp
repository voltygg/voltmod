#pragma once

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>

namespace $namespace
{

/**
 * Everything this plugin owns for one load cycle. VoltMod::AppPlugin builds it on load and drops
 * it on unload, so nothing survives a `volt reload`. Members are destroyed in reverse order.
 */
struct App
{
    explicit App(VoltMod::Runtime& runtime) : Runtime(runtime) {}

    /** Load config and register commands. False aborts the plugin load. */
    bool Start();

    VoltMod::Runtime& Runtime;
    ConfigManager Config;

private:
    /** Declared last, so handlers stop before the state they capture goes away. */
    VoltMod::Subscriptions _subs;
};

}  // namespace $namespace
