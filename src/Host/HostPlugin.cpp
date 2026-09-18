#include "Host/HostPlugin.hpp"

#include "Engine/Server/ConsoleLogger.hpp"
#include "Host/EngineHooks.hpp"
#include "Host/HostGameData.hpp"
#include "Host/HostSchema.hpp"
#include "Host/PluginHost.hpp"
#include "Host/PluginLoader.hpp"

#include <ISmmAPI.h>
#include <VoltMod/BuildInfo.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <string_view>

// The host really is a Metamod plugin, so it keeps its own set of Metamod globals. PLUGIN_EXPOSE
// at the bottom of this file defines them, including KHook's dispatch pointer.
PLUGIN_GLOBALVARS();

namespace VoltMod
{

static constexpr const char* LogTag = "VoltMod";
static constexpr std::string_view GameDataPath = "addons/voltmod/gamedata/gamedata.jsonc";

HostPlugin::HostPlugin() = default;

HostPlugin::~HostPlugin()
{
    Shutdown();
}

bool HostPlugin::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    Log::SetHandler(MakeConsoleHandler(LogTag));
    SetBaseDir(ismm->GetBaseDir());

    // Once for the process, before any plugin: a signature a game update broke is logged here
    // and nowhere else. Another Metamod plugin may already hold a slot, so read through KHook.
    _gameData = std::make_unique<HostGameData>();
    _gameData->Resolve(GameDataPath, ReadOriginalSlot);

    _host =
        std::make_unique<PluginHost>(ismm, KHook::__exported__khook, _gameData->Ready() ? _gameData.get() : nullptr);
    // Once for the process too: the baked offsets are the same in every plugin built with this
    // host, and each plugin compares its layout stamp with what was checked here.
    _schema = std::make_unique<HostSchema>();
    _schema->Start(ismm, *_host, _host->GameData());

    _plugins = std::make_unique<PluginLoader>(*_host);
    _hooks = std::make_unique<EngineHooks>(
        *_host, [this] { _plugins->RunQueuedRequests(); }, [this] { _schema->OnServerStartup(); });

    if (Status started = _hooks->Start(ismm); !started)
    {
        ismm->Format(error, maxlen, "%s", started.error().Detail.c_str());
        Shutdown();
        return false;
    }

    _plugins->Start();

    Log::Info("VoltMod host {} ({}, committed {}) loaded{}.", BuildInfo::Version, BuildInfo::RepoCommit,
              BuildInfo::BuildDate, late ? " (late)" : "");
    return true;
}

bool HostPlugin::Unload(char* error, size_t maxlen)
{
    Shutdown();
    return true;
}

void HostPlugin::Shutdown()
{
    // Plugins first: their own teardown runs while the host's events and services are still there.
    if (_plugins)
        _plugins->Stop();
    if (_hooks)
        _hooks->Stop();

    _hooks.reset();
    _plugins.reset();
    _schema.reset();
    _host.reset();
    _gameData.reset();
}

const char* HostPlugin::GetAuthor()
{
    return "Sukhrob Ilyosbekov";
}
const char* HostPlugin::GetName()
{
    return "VoltMod";
}
const char* HostPlugin::GetDescription()
{
    return "Loads VoltMod plugins and serves them one set of engine hooks.";
}
const char* HostPlugin::GetURL()
{
    return "https://github.com/voltygg/voltmod";
}
const char* HostPlugin::GetLicense()
{
    return "MIT";
}
const char* HostPlugin::GetVersion()
{
    return BuildInfo::Version;
}
const char* HostPlugin::GetDate()
{
    return BuildInfo::BuildDate;
}
const char* HostPlugin::GetLogTag()
{
    return LogTag;
}

}  // namespace VoltMod

static VoltMod::HostPlugin g_hostPlugin;
PLUGIN_EXPOSE(HostPlugin, g_hostPlugin);
