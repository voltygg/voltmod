#include "Plugin.hpp"

#include "Config.hpp"
#include "Managers.hpp"

#include <CS2Kit/Api.hpp>
#include <CS2Kit/Core/PluginInfoStamp.hpp>

CS2KIT_PLUGIN($klass, $ns);

CS2Kit::PluginInfo $klass::Info() const
{
    return CS2Kit::WithBuildInfo({
        .Name = "$title",
        .Author = "TODO",
        .Description = "TODO",
        .LogTag = "$tag",
    });
}

bool $klass::OnLoad(bool late)
{
    // Commands in Commands.cpp self-register and the kit ingests them after this
    // returns; chat dispatch, permissions, and replies default to allow /
    // Engine().Messages.Reply until the plugin sets Engine().Policy.
    return CS2Kit::LoadStandardConfig($ns::App().Config, {.Addon = "$name"});
}
