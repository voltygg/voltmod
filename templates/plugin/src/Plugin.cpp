#include "Plugin.hpp"

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

VOLTMOD_PLUGIN($klass);

VoltMod::PluginInfo $klass::Info() const
{
    return VoltMod::WithBuildInfo({
        .Name = "$title",
        .Author = "TODO",
        .Description = "TODO",
        .LogTag = "$tag",
    });
}

bool $klass::OnLoad(VoltMod::Runtime& runtime, bool late)
{
    _app.emplace(runtime);
    return _app->Start();
}
