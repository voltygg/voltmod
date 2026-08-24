#pragma once

#include "App.hpp"

#include <VoltMod/Api.hpp>
#include <optional>

/**
 * $title plugin entry point. VoltMod::MetamodPlugin owns the Metamod lifecycle, standard
 * hooks, player tracking and chat-command dispatch; this class adds the metadata and owns
 * the plugin's object graph for one load cycle.
 */
class $klass final : public VoltMod::MetamodPlugin
{
protected:
    VoltMod::PluginInfo Info() const override;
    bool OnLoad(VoltMod::Runtime& runtime, bool late) override;
    void OnUnload() override { _app.reset(); }

private:
    std::optional<$ns::App> _app;
};
