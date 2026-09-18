#include <VoltMod/App/Internal/PluginModule.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <algorithm>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <vector>

namespace VoltMod::Internal
{

/** The C function the host calls for @p Member. Nothing may unwind into the host. */
template <auto Member>
struct HostCallback;

template <class Result, class... Args, Result (PluginModule::*Member)(Args...)>
struct HostCallback<Member>
{
    static Result Call(void* module, Args... args) noexcept
    {
        try
        {
            return (static_cast<PluginModule*>(module)->*Member)(args...);
        }
        catch (const std::exception& error)
        {
            Log::Error("Unhandled exception in a host event: {}", error.what());
            return Result();
        }
        catch (...)
        {
            Log::Error("Unhandled non-standard exception in a host event.");
            return Result();
        }
    }
};

PluginModule::~PluginModule()
{
    Detach();
}

bool PluginModule::Attach(IHost& host, const PluginBuild& build, char* error, size_t errorSize) noexcept
{
    try
    {
        const bool loaded = AttachImpl(host, build, error, errorSize);
        if (!loaded)
            _host = nullptr;
        return loaded;
    }
    catch (const std::exception& exception)
    {
        WriteFailure(error, errorSize, exception.what());
        Shutdown();
        _host = nullptr;
        return false;
    }
    catch (...)
    {
        WriteFailure(error, errorSize, "plugin threw a non-standard exception during load");
        Shutdown();
        _host = nullptr;
        return false;
    }
}

bool PluginModule::AttachImpl(IHost& host, const PluginBuild& build, char* error, size_t errorSize)
{
    KHook::__exported__khook = host.HookDispatcher();
    _host = &host;

    _runtime = std::make_unique<Runtime>();
    // Attach before Load so a load step can already reach a peer's published interface.
    _runtime->Exchange.Attach(&host.Services());
    _runtime->Commands.Attach(&host);

    const LoadContext context{.Host = &host, .Version = build.Version, .Error = error, .MaxLen = errorSize};
    if (!_runtime->Initialize(context))
    {
        if (_runtime->LoadSteps.Count() > 0)
            Log::Info("{}", _runtime->LoadSteps.Summary());
        _runtime.reset();
        return false;
    }

    _runtime->Status.RegisterSection("build", [name = _runtime->PluginName, build] {
        return Json::Write(glz::obj{"name", name, "version", std::string_view(build.Version), "commit",
                                    std::string_view(build.Commit), "date", std::string_view(build.Date)});
    });

    _plugin = _factory(*_runtime);
    if (!_plugin)
    {
        WriteFailure(error, errorSize, "plugin factory returned nothing");
        Shutdown();
        return false;
    }

    SubscribeHostEvents();

    if (!_plugin->Load())
    {
        std::string failure = _runtime->LoadSteps.AbortReason();
        if (failure.empty())
            failure = "Load returned false";
        Log::Info("{}", _runtime->LoadSteps.Summary());
        WriteFailure(error, errorSize, failure);
        Shutdown();
        return false;
    }

    _runtime->LoadSteps.Optional("Permissions", [this]() -> Status {
        const std::vector<std::string> missing = _runtime->Commands.CommandsMissingPolicy();
        if (missing.empty())
            return {};
        return std::unexpected(
            Error::Invalid(std::format("{} command(s) gate on a permission with no HasPermission policy "
                                       "installed and will be denied ({}); set Runtime::Policy.HasPermission "
                                       "in Load",
                                       missing.size(), Strings::Join(missing, ", "))));
    });

    Log::Info("{}", _runtime->LoadSteps.Summary());
    return true;
}

void PluginModule::WriteFailure(char* error, size_t errorSize, std::string_view failure) noexcept
{
    if (error == nullptr || errorSize == 0)
        return;

    const size_t length = std::min(errorSize - 1, failure.size());
    std::memcpy(error, failure.data(), length);
    error[length] = '\0';
}

void PluginModule::Detach() noexcept
{
    Shutdown();
    _host = nullptr;
}

// Stop commands before destroying their captured state, then drop host events and services.
void PluginModule::Shutdown() noexcept
{
    if (_runtime)
        _runtime->Commands.RemoveAll();
    _plugin.reset();
    _hostEvents.Clear();
    _runtime.reset();
}

const char* PluginModule::StatusJson() noexcept
{
    try
    {
        _status = _runtime ? _runtime->Status.BuildJson() : std::string("{}");
        return _status.c_str();
    }
    catch (...)
    {
        return "{}";
    }
}

void PluginModule::SubscribeHostEvents()
{
    IHostEvents& events = _host->Events();
    auto keep = [&](uint64_t token) { _hostEvents.Add(Subscription([&events, token] { events.Unsubscribe(token); })); };

    keep(events.OnFrame(&HostCallback<&PluginModule::HostFrame>::Call, this));
    keep(events.OnServerStartup(&HostCallback<&PluginModule::HandleServerStartup>::Call, this));
    keep(events.OnClientConnected(&HostCallback<&PluginModule::HostClientConnected>::Call, this));
    keep(events.OnClientDisconnected(&HostCallback<&PluginModule::HostClientDisconnected>::Call, this));
    keep(events.OnClientFullyConnected(&HostCallback<&PluginModule::HostClientFullyConnected>::Call, this));
    keep(events.OnClientSettingsChanged(&HostCallback<&PluginModule::HostClientSettingsChanged>::Call, this));
    keep(events.OnConsoleCommand(&HostCallback<&PluginModule::HandleConsoleCommand>::Call, this));
    keep(events.OnCheckTransmit(&HostCallback<&PluginModule::HostCheckTransmit>::Call, this));
}

void PluginModule::HostFrame()
{
    // `volt log` changes this; reading it once a frame lets the log helpers skip silenced lines.
    Log::SetMinimumLevel(static_cast<LogLevel>(_host->MinLogLevel()));
    _runtime->OnGameFrame();
}

void PluginModule::HandleServerStartup(std::string_view mapName)
{
    Log::Info("Server startup: map '{}'.", mapName.empty() ? std::string_view("<none>") : mapName);
    _runtime->Map.SetCurrent(std::string(mapName));
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    _plugin->OnServerStartup(mapName);
}

void PluginModule::HostClientConnected(int slot, int64_t steamId, std::string_view name, std::string_view address)
{
    _runtime->Players.Add(slot, steamId, std::string(name), std::string(address));
}

void PluginModule::HostClientDisconnected(int slot)
{
    _runtime->Players.Remove(slot);
}

void PluginModule::HostClientFullyConnected(int slot)
{
    _runtime->Hooks.ClientConVars.OnClientFullyConnect(slot);
    _runtime->Players.OnClientFullyConnected(slot);
}

void PluginModule::HostClientSettingsChanged(int slot)
{
    _runtime->Players.OnClientSettingsChanged(slot);
}

void PluginModule::HostCheckTransmit(CCheckTransmitInfo** infoList, int infoCount)
{
    _runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
}

}  // namespace VoltMod::Internal
