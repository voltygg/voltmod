#include <VoltMod/App/Plugin.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Text/Strings.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Players/Player.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Runtime.hpp>
#include <cstdio>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace VoltMod
{

static std::string_view Text(HostString text)
{
    return text.Data ? std::string_view(text.Data, text.Length) : std::string_view();
}

static HostString Borrow(std::string_view text)
{
    return HostString{.Data = text.data(), .Length = text.size()};
}

/** Nothing may leave a plugin and re-enter the host: the two modules do not share a runtime. */
static void ReportEscaped(const char* event, const std::exception& error)
{
    Log::Error("Unhandled exception in {}: {}", event, error.what());
}

Plugin::Plugin() = default;
Plugin::~Plugin() = default;

bool Plugin::Attach(IHost& host, char* error, size_t errorSize)
{
    // KHook resolves its entry points against this module's own pointer, so seed it before any
    // hook goes up.
    KHook::__exported__khook = host.Detours();

    _host = &host;
    _log = static_cast<IHostLog*>(host.GetInterface(Borrow(IHostLog::InterfaceName)));
    _events = static_cast<IHostEvents*>(host.GetInterface(Borrow(IHostEvents::InterfaceName)));
    if (!_events)
    {
        snprintf(error, errorSize, "The host did not offer %s", IHostEvents::InterfaceName);
        return false;
    }

    _info = Info();
    _runtime = std::make_unique<Runtime>();
    // Attach before Start so a load step can already reach a peer's published interface.
    _runtime->Exchange.Attach(static_cast<IHostServices*>(host.GetInterface(Borrow(IHostServices::InterfaceName))));
    _runtime->Commands.Attach(&host);

    const LoadContext context{.Host = &host, .Error = error, .MaxLen = errorSize, .LogPrefix = _info.LogTag};
    if (!_runtime->Start(context))
    {
        if (_runtime->LoadSteps.Count() > 0)
            Log::Info("{}", _runtime->LoadSteps.Summary());
        _runtime.reset();
        return false;
    }

    _runtime->Status.RegisterSection("build", [info = _info] {
        return Json::Write(
            glz::obj{"name", info.Name, "version", info.Version, "commit", info.Commit, "date", info.Date});
    });

    SubscribeHostEvents();
    OnRegisterHooks(*_runtime, _customHooks);

    if (!OnLoad(*_runtime))
    {
        // Supply a reason when OnLoad returns false without recording one.
        std::string failure = _runtime->LoadSteps.AbortReason();
        if (failure.empty())
            failure = "OnLoad returned false";
        Log::Info("{}", _runtime->LoadSteps.Summary());
        snprintf(error, errorSize, "%s", failure.c_str());
        Shutdown();
        return false;
    }

    // Report permission-gated commands that have no policy.
    _runtime->LoadSteps.Optional("Permissions", [this]() -> Status {
        const std::vector<std::string> missing = _runtime->Commands.CommandsMissingPolicy();
        if (missing.empty())
            return {};
        return std::unexpected(
            Error::Invalid(std::format("{} command(s) gate on a permission with no HasPermission policy "
                                       "installed and will be denied ({}); set Runtime::Policy.HasPermission "
                                       "in OnLoad",
                                       missing.size(), Strings::Join(missing, ", "))));
    });

    Log::Info("{}", _runtime->LoadSteps.Summary());
    if (!_info.Commit.empty())
        Log::Info("Loaded {} v{} ({}, committed {}).", _info.Name, _info.Version, _info.Commit, _info.Date);
    else
        Log::Info("Loaded successfully.");
    return true;
}

void Plugin::Detach()
{
    Shutdown();
    _events = nullptr;
    _log = nullptr;
    _host = nullptr;
}

// Stop callbacks into plugin state before OnUnload, then drop the host events and the services.
void Plugin::Shutdown()
{
    _customHooks.Clear();
    if (_runtime)
        _runtime->Commands.RemoveAll();
    OnUnload();
    _hostEvents.Clear();
    _runtime.reset();
}

const char* Plugin::StatusJson()
{
    _status = _runtime ? _runtime->Status.BuildJson() : std::string("{}");
    return _status.c_str();
}

void Plugin::SubscribeHostEvents()
{
    // The host installed each engine hook once. Taking them in the order the framework used to
    // install its own keeps the load-time sequence a reader already knows.
    auto take = [this](uint64_t token) {
        _hostEvents.Add(Subscription([events = _events, token] { events->Unsubscribe(token); }));
    };

    take(_events->OnFrame(&Plugin::OnHostFrame, this));
    take(_events->OnServerStartup(&Plugin::OnHostServerStartup, this));
    take(_events->OnClientConnected(&Plugin::OnHostClientConnected, this));
    take(_events->OnClientDisconnected(&Plugin::OnHostClientDisconnected, this));
    take(_events->OnClientFullyConnected(&Plugin::OnHostClientFullyConnected, this));
    take(_events->OnClientSettingsChanged(&Plugin::OnHostClientSettingsChanged, this));
    take(_events->OnConsoleCommand(&Plugin::OnHostConsoleCommand, this));
    take(_events->OnCheckTransmit(&Plugin::OnHostCheckTransmit, this));
}

void Plugin::OnHostFrame(void* context)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        // `volt log` lands here: reading it once a frame is what lets the log helpers skip
        // formatting a line this plugin is silenced for.
        if (self->_log)
            Log::SetMinimumLevel(static_cast<LogLevel>(self->_log->MinLevel()));

        self->_runtime->OnGameFrame();
    }
    catch (const std::exception& error)
    {
        ReportEscaped("the frame tick", error);
    }
}

void Plugin::OnHostServerStartup(void* context, HostString mapName)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        self->HandleServerStartup(Text(mapName));
    }
    catch (const std::exception& error)
    {
        ReportEscaped("server startup", error);
    }
}

void Plugin::OnHostClientConnected(void* context, int slot, int64_t steamId, HostString name, HostString address)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        // Capture the connection address while the engine still provides it.
        self->_runtime->Players.Add(slot, steamId, std::string(Text(name)), std::string(Text(address)));
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a client connecting", error);
    }
}

void Plugin::OnHostClientDisconnected(void* context, int slot)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        // Remove raises Disconnected before the slot can be reused.
        self->_runtime->Players.Remove(slot);
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a client disconnecting", error);
    }
}

void Plugin::OnHostClientFullyConnected(void* context, int slot)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        self->_runtime->Hooks.ClientConVars.OnClientFullyConnect(slot);
        self->_runtime->Players.OnClientFullyConnected(slot);
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a client fully connecting", error);
    }
}

void Plugin::OnHostClientSettingsChanged(void* context, int slot)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        self->_runtime->Players.OnClientSettingsChanged(slot);
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a client settings change", error);
    }
}

bool Plugin::OnHostConsoleCommand(void* context, HostString name, HostString arguments, int slot)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        return self->HandleConsoleCommand(Text(name), Text(arguments), slot);
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a console command", error);
        return false;
    }
}

void Plugin::OnHostCheckTransmit(void* context, CCheckTransmitInfo** infoList, int infoCount)
{
    auto* self = static_cast<Plugin*>(context);
    try
    {
        self->_runtime->Hooks.Visibility.OnCheckTransmit(infoList, infoCount);
    }
    catch (const std::exception& error)
    {
        ReportEscaped("a transmit check", error);
    }
}

bool Plugin::OnPlayerChat(Player* player, std::string_view message, bool /*teamChat*/)
{
    // Menu input takes precedence over command parsing.
    if (_runtime->Hooks.ChatInput.TryConsume(player->Slot(), message))
        return true;

    return _runtime->Commands.HandleChatMessage(player, message);
}

void Plugin::HandleServerStartup(std::string_view mapName)
{
    Log::Info("Server startup: map '{}'.", mapName.empty() ? std::string_view("<none>") : mapName);
    _runtime->Map.SetCurrent(std::string(mapName));
    // Publish the new entity system before the plugin callback.
    _runtime->Entities.OnServerStartup();
    _runtime->GameEvents.OnServerStartup();
    _runtime->Hooks.ClientConVars.OnServerStartup();
    OnServerStartup(mapName);
}

bool Plugin::HandleConsoleCommand(std::string_view name, std::string_view arguments, int slot)
{
    if (name == "vote")
    {
        // A ballot for a plugin vote never reaches the engine's own vote controller.
        return _runtime->Hooks.Vote.TryCastBallot(slot, arguments);
    }

    const bool isSay = name == "say";
    const bool isSayTeam = name == "say_team";
    if (!isSay && !isSayTeam)
        return false;

    std::string_view message = arguments;
    if (message.size() >= 2 && message.front() == '"' && message.back() == '"')
    {
        message.remove_prefix(1);
        message.remove_suffix(1);
    }
    if (message.empty() || !IsValidSlot(slot))
        return false;

    Player* player = _runtime->Players.Get(slot);
    if (!player)
        return false;

    // Keep handled chat lines out of the game's say handler.
    return OnPlayerChat(player, message, isSayTeam);
}

}  // namespace VoltMod
