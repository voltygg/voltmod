#pragma once

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/Core/Signals/Subscriptions.hpp>

namespace $namespace
{

/**
 * Everything this plugin owns for one load cycle. VoltMod destroys it before the runtime, so
 * nothing survives a `volt reload`. Members are destroyed in reverse order.
 */
struct App final : VoltMod::Plugin
{
    using Plugin::Plugin;

    /** Register commands. False aborts the plugin load. */
    bool Load() override;

    /** Loaded first, so every member below is built with settings. */
    ConfigManager Config = VoltMod::LoadConfig<ConfigManager>(Runtime);

private:
    /** Declared last, so handlers stop before the state they capture goes away. */
    VoltMod::Subscriptions _subs;
};

}  // namespace $namespace
