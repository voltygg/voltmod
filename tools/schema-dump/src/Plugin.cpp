#include "Plugin.hpp"

#include "Config.hpp"

#include <VoltMod/Api.hpp>
#include <VoltMod/App/PluginInfoStamp.hpp>

VOLTMOD_PLUGIN(SchemaDumpPlugin);

VoltMod::PluginInfo SchemaDumpPlugin::Info() const
{
    return VoltMod::WithBuildInfo({
        .Name = "Schema Dump",
        .Author = "Sukhrob Ilyosbekov",
        .Description = "Dev-only: dumps the engine schema to JSON for voltmod schemagen.",
        .LogTag = "SCHEMADUMP",
    });
}

bool SchemaDumpPlugin::OnLoad(VoltMod::Runtime& runtime)
{
    _app.emplace(runtime);
    return _app->Start();
}
