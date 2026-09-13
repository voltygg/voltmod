#include "Plugin.hpp"

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

VOLTMOD_PLUGIN($plugin_class);

VoltMod::PluginInfo $plugin_class::Info() const
{
    return VoltMod::WithBuildInfo({
        .Name = "$title",
        .Author = "TODO",
        .Description = "TODO",
        .LogTag = "$tag",
    });
}

bool $plugin_class::OnLoad(VoltMod::Runtime& runtime)
{
    _app.emplace(runtime);
    return _app->Start();
}
