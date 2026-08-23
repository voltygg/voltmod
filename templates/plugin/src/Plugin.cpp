#include "Plugin.hpp"

#include "Config.hpp"

#include <CS2Kit/Api.hpp>
#include <CS2Kit/Core/PluginInfoStamp.hpp>

CS2KIT_PLUGIN($klass);

CS2Kit::PluginInfo $klass::Info() const
{
    return CS2Kit::WithBuildInfo({
        .Name = "$title",
        .Author = "TODO",
        .Description = "TODO",
        .LogTag = "$tag",
    });
}

bool $klass::OnLoad(CS2Kit::Runtime& runtime, bool late)
{
    _app.emplace(runtime);
    return _app->Start();
}
