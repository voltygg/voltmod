#include "Host/MetamodEntry.hpp"

#include "Engine/Server/ConsoleLogger.hpp"

#include <ISmmAPI.h>
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

MetamodEntry::MetamodEntry() = default;

MetamodEntry::~MetamodEntry()
{
    Shutdown();
}

bool MetamodEntry::Load(PluginId id, ISmmAPI* ismm, char* error, size_t maxlen, bool late)
{
    PLUGIN_SAVEVARS();

    Log::SetHandler(MakeConsoleHandler(LogTag));
    SetBaseDir(ismm->GetBaseDir());

    // Once for the process, before any plugin: a signature a game update broke is logged here
    // and nowhere else. Another Metamod plugin may already hold a slot, so read through KHook.
    _gameData = std::make_unique<GameDataService>();
    _gameData->Resolve(GameDataPath, ReadOriginalSlot);

    _host =
        std::make_unique<PluginHost>(ismm, KHook::__exported__khook, _gameData->Ready() ? _gameData.get() : nullptr);
    // Once for the process too: the baked offsets are the same in every plugin built with this
    // host, and each plugin compares its layout stamp with what was checked here.
    _schema = std::make_unique<SchemaService>();
    _schema->Initialize(ismm, *_host, _host->GameData());

    _plugins = std::make_unique<PluginLoader>(*_host);
    _hooks = std::make_unique<EngineHooks>(
        *_host, [this] { _plugins->RunPending(); }, [this] { _schema->OnServerStartup(); });

    if (Status installed = _hooks->Install(ismm); !installed)
    {
        ismm->Format(error, maxlen, "%s", installed.error().Detail.c_str());
        Shutdown();
        return false;
    }

    // Before the plugins load, so one of them registering `volt` is refused rather than racing it.
    _command = std::make_unique<VoltCommand>(*_host, *_plugins);
    _plugins->LoadAll();

    Log::Info("VoltMod host {} loaded{}.", VOLTMOD_VERSION, late ? " (late)" : "");
    return true;
}

bool MetamodEntry::Unload(char* error, size_t maxlen)
{
    Shutdown();
    return true;
}

void MetamodEntry::Shutdown()
{
    // Plugins first: their own teardown runs while the host's events and services are still there.
    if (_plugins)
        _plugins->UnloadAll();
    if (_hooks)
        _hooks->Uninstall();

    _hooks.reset();
    _command.reset();
    _plugins.reset();
    _schema.reset();
    _host.reset();
    _gameData.reset();
}

const char* MetamodEntry::GetAuthor()
{
    return "Sukhrob Ilyosbekov";
}
const char* MetamodEntry::GetName()
{
    return "VoltMod";
}
const char* MetamodEntry::GetDescription()
{
    return "Loads VoltMod plugins and serves them one set of engine hooks.";
}
const char* MetamodEntry::GetURL()
{
    return "https://github.com/voltygg/voltmod";
}
const char* MetamodEntry::GetLicense()
{
    return "MIT";
}
const char* MetamodEntry::GetVersion()
{
    return VOLTMOD_VERSION;
}
// No build date: a wall-clock stamp would defeat ccache.
const char* MetamodEntry::GetDate()
{
    return "";
}
const char* MetamodEntry::GetLogTag()
{
    return LogTag;
}

}  // namespace VoltMod

static VoltMod::MetamodEntry g_voltmodHost;
PLUGIN_EXPOSE(MetamodEntry, g_voltmodHost);
