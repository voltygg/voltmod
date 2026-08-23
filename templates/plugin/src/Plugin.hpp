#pragma once

#include "App.hpp"

#include <CS2Kit/Api.hpp>
#include <optional>

/**
 * $title plugin entry point. CS2Kit::MetamodPlugin owns the Metamod lifecycle, standard
 * hooks, player tracking and chat-command dispatch; this class adds the metadata and owns
 * the plugin's object graph for one load cycle.
 */
class $klass final : public CS2Kit::MetamodPlugin
{
protected:
    CS2Kit::PluginInfo Info() const override;
    bool OnLoad(CS2Kit::Runtime& runtime, bool late) override;
    void OnUnload() override { _app.reset(); }

private:
    std::optional<$ns::App> _app;
};
