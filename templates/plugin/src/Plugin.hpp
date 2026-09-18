#pragma once

#include "App.hpp"

#include <VoltMod/Api.hpp>
#include <optional>

/**
 * $title plugin entry point. VoltMod::Plugin owns one Runtime per load cycle, the
 * subscriptions to the host's engine events, player tracking and chat-command dispatch; this
 * class adds the metadata and owns the plugin's object graph for that cycle.
 */
class $plugin_class final : public VoltMod::Plugin
{
protected:
    VoltMod::PluginInfo Info() const override;
    bool OnLoad(VoltMod::Runtime& runtime) override;
    void OnUnload() override { _app.reset(); }

private:
    std::optional<$namespace::App> _app;
};
