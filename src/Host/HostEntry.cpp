#include "Engine/Memory/ScriptBindings.hpp"
#include "Engine/Server/ConsoleLogger.hpp"
#include "Host/EngineHooks.hpp"
#include "Host/GameData/GameDataService.hpp"
#include "Host/HostStart.hpp"
#include "Host/Loading/PluginLoader.hpp"
#include "Host/Loading/VoltCommand.hpp"
#include "Host/Plugins/PluginHost.hpp"
#include "Host/Schema/SchemaService.hpp"

#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Result.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Host/PluginDescriptor.hpp>
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string_view>

namespace KHook
{
KHook::IKHook* __exported__khook = nullptr;
}

namespace VoltMod
{

static constexpr std::string_view GameDataPath = "addons/voltmod/gamedata/gamedata.jsonc";

/** The host's services for the process, built at start and torn down at stop. */
class HostEntry
{
public:
    ~HostEntry() { Stop(); }

    Status Start(const HostStart& start)
    {
        KHook::__exported__khook = start.HookDispatcher;
        Log::SetHandler(MakeConsoleHandler("VoltMod"));
        SetBaseDir(start.GameDir);

        // Once for the process, before any plugin: a signature a game update broke is logged here
        // and nowhere else. A hook may already hold a slot, so read the original through KHook.
        _gameData = std::make_unique<GameDataService>();
        _gameData->Resolve(GameDataPath, ReadOriginalSlot, FindScriptBinding);

        _host = std::make_unique<PluginHost>(start, _gameData->Ready() ? _gameData.get() : nullptr);
        // Once for the process too: every plugin built with this host carries the same baked offsets.
        _schema = std::make_unique<SchemaService>();
        _schema->Initialize(*_host, _host->GameData());

        _plugins = std::make_unique<PluginLoader>(*_host);
        _hooks = std::make_unique<EngineHooks>(
            *_host, [this] { _plugins->RunPending(); }, [this] { _schema->OnServerStartup(); });

        if (Status installed = _hooks->Install(); !installed)
        {
            Stop();
            return installed;
        }

        // Before the plugins load, so a plugin registering `volt` is refused rather than racing it.
        _command = std::make_unique<VoltCommand>(*_host, *_plugins);
        _plugins->LoadAll();

        Log::Info("VoltMod host {} loaded.", VOLTMOD_VERSION);
        return {};
    }

    /** Take the plugins down, then the hooks, then the host's own state. */
    void Stop()
    {
        // Plugins first: their teardown runs while the host's events and services are still there.
        if (_plugins)
        {
            _plugins->UnloadAll();
        }
        if (_hooks)
        {
            _hooks->Uninstall();
        }

        _hooks.reset();
        _command.reset();
        _plugins.reset();
        _schema.reset();
        _host.reset();
        _gameData.reset();
    }

private:
    // In build order, so destruction runs backwards and the hooks stop before the plugins unload.
    std::unique_ptr<GameDataService> _gameData;
    std::unique_ptr<PluginHost> _host;
    std::unique_ptr<SchemaService> _schema;
    std::unique_ptr<PluginLoader> _plugins;
    std::unique_ptr<VoltCommand> _command;
    std::unique_ptr<EngineHooks> _hooks;
};

static HostEntry g_host;

}  // namespace VoltMod

extern "C" VOLTMOD_EXPORT bool VoltMod_HostStart(const VoltMod::HostStart* start, char* error, size_t errorSize)
{
    VoltMod::Status started = VoltMod::g_host.Start(*start);
    if (!started && errorSize > 0)
    {
        const std::string_view detail = started.error().Detail;
        const size_t length = std::min(errorSize - 1, detail.size());
        std::memcpy(error, detail.data(), length);
        error[length] = '\0';
    }
    return started.has_value();
}

extern "C" VOLTMOD_EXPORT void VoltMod_HostStop()
{
    VoltMod::g_host.Stop();
}
