#include "Schema/Layout.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Host/IHostGameData.hpp>
#include <VoltMod/Runtime.hpp>
#include <chrono>
#include <cstdint>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <map>
#include <networksystem/inetworkmessages.h>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <tier1/convar.h>

namespace VoltMod
{

Runtime::Runtime() = default;

Runtime::~Runtime()
{
    // Stop HTTP workers and flush queued logs before removing frame hooks.
    Http.Stop();
    Log::DeliverPending();
}

std::string Runtime::PluginFile(std::string_view relative) const
{
    return VoltMod::PluginFile(PluginName, relative);
}

bool Runtime::Initialize(const LoadContext& context)
{
    PluginName = context.Host->Name();
    Version = context.Host->Version();
    InstallLogger(context);

    if (!ResolveInterfaces(context))
        return false;

    if (!InitializeServices(context))
        return false;

    RegisterStatusSections();
    return true;
}

void Runtime::InstallLogger(const LoadContext& context)
{
    // The host owns the console and prefixes this plugin's log tag.
    IHost* host = context.Host;
    Log::SetMinimumLevel(static_cast<LogLevel>(host->MinLogLevel()));
    Log::SetHandler(
        [host](LogLevel level, std::string_view message) { host->WriteLog(static_cast<uint8_t>(level), message); });

    SetBaseDir(context.Host->Metamod()->GetBaseDir());
}

bool Runtime::ResolveInterfaces(const LoadContext& context)
{
    ISmmAPI* ismm = context.Host->Metamod();

    auto resolveEngine = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetEngineFactory(), version, 0);
    };
    auto resolveServer = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetServerFactory(), version, 0);
    };

    auto& gi = Unsafe.Interfaces;

#define VOLTMOD_RESOLVE(field, factory, version)                                              \
    gi.field = static_cast<decltype(gi.field)>(factory(version));                             \
    if (!gi.field)                                                                            \
    {                                                                                         \
        ismm->Format(context.Error, context.MaxLen, "Could not find interface: %s", version); \
        return false;                                                                         \
    }

    VOLTMOD_RESOLVE(ServerGameDLL, resolveServer, INTERFACEVERSION_SERVERGAMEDLL)
    VOLTMOD_RESOLVE(ServerGameClients, resolveServer, INTERFACEVERSION_SERVERGAMECLIENTS)
    VOLTMOD_RESOLVE(NetworkServerService, resolveEngine, NETWORKSERVERSERVICE_INTERFACE_VERSION)
    VOLTMOD_RESOLVE(GameEntities, resolveServer, INTERFACEVERSION_SERVERGAMEENTS)
    VOLTMOD_RESOLVE(Engine, resolveEngine, INTERFACEVERSION_VENGINESERVER)
    VOLTMOD_RESOLVE(GameEventSystem, resolveEngine, GAMEEVENTSYSTEM_INTERFACE_VERSION)
    VOLTMOD_RESOLVE(NetworkMessages, resolveEngine, NETWORKMESSAGES_INTERFACE_VERSION)
    VOLTMOD_RESOLVE(SchemaSystem, resolveEngine, SCHEMASYSTEM_INTERFACE_VERSION)
    VOLTMOD_RESOLVE(CVar, resolveEngine, CVAR_INTERFACE_VERSION)
    VOLTMOD_RESOLVE(GameResourceService, resolveEngine, GAMERESOURCESERVICESERVER_INTERFACE_VERSION)

#undef VOLTMOD_RESOLVE

    // Register pending tier1 ConCommands before the engine invokes ServerCommand instances.
    g_pCVar = gi.CVar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    return true;
}

bool Runtime::InitializeServices(const LoadContext& context)
{
    // Plugin logs the summary and required-step failure.
    auto& steps = LoadSteps;

    // The host read and scanned gamedata once for the process; this only takes the numbers.
    steps.Optional("GameData", [&]() -> VoltMod::Status {
        IHostGameData* gameData = context.Host->GameData();
        if (!gameData)
            return std::unexpected(Error::Engine("the host has no gamedata"));

        return Unsafe.Bindings.Bind(
            [gameData](GameDataSection sections, std::string_view name) { return gameData->Lookup(sections, name); });
    });

    // A required-step failure is reported to Metamod and aborts the load.
    auto requiredStep = [&](std::string_view name, const std::function<VoltMod::Status()>& step) {
        if (steps.Required(name, step))
            return true;

        context.Host->Metamod()->Format(context.Error, context.MaxLen, "%s", steps.AbortReason().c_str());
        return false;
    };

    if (!requiredStep("Messages", [&] { return Messages.Initialize(); }))
        return false;

    // Abort on schema drift, which the host found once for the process.
    if (!requiredStep("SchemaLayout", [&] { return TakeHostSchema(context); }))
        return false;

    // StartupServer resolves CGameEntitySystem when the first map loads.
    steps.Optional("Entities", [&] { return Entities.Initialize(); });
    steps.Optional("Precache",
                   [&] { return World.Precache.Initialize(std::format("{}_VoltModPrecache", PluginName)); });
    steps.Optional("ConVars", [&] { return ConVars.Initialize(); });
    steps.Optional("GameEvents", [&] { return GameEvents.Initialize(); });
    steps.Optional("ClientConVars", [&] { return Hooks.ClientConVars.Initialize(); });

    for (const auto& [feature, reason] : UnavailableFeatures())
        Log::Warn("{} is unavailable: {}", feature, reason);
    return true;
}

VoltMod::Status Runtime::TakeHostSchema(const LoadContext& context)
{
    const IHost* schema = context.Host;

    // The offsets are baked into this plugin's own copy of the SDK, so the host's check only covers
    // it when both were generated from the same layout.
    if (schema->SchemaLayoutStamp() != Schema::GeneratedLayoutStamp())
    {
        return std::unexpected(Error::Invalid(
            std::format("this plugin was built against another schema layout (plugin {:016X}, host {:016X}); "
                        "rebuild it against this VoltMod",
                        Schema::GeneratedLayoutStamp(), schema->SchemaLayoutStamp())));
    }

    if (!schema->SchemaVerified())
        return std::unexpected(Error::Invalid("the host found schema drift; its log names every field"));
    return {};
}

std::map<std::string, std::string> Runtime::UnavailableFeatures() const
{
    const std::pair<std::string_view, VoltMod::Status> features[] = {
        {"Movement", Hooks.Movement.Available()},           {"Teleport", Hooks.Teleport.Available()},
        {"Visibility", Hooks.Visibility.Available()},       {"Trace", World.Trace.Available()},
        {"ClientConVars", Hooks.ClientConVars.Available()}, {"Screens", Screens.Available()},
    };

    std::map<std::string, std::string> unavailable;
    for (const auto& [feature, available] : features)
    {
        if (!available)
            unavailable.emplace(feature, available.error().Detail);
    }
    return unavailable;
}

void Runtime::RegisterStatusSections()
{
    // Plugins add status sections during Load; the runtime owns them for the load cycle.
    Status.RegisterSection("load", [this] {
        std::map<std::string, std::string> failed;
        for (const FailedStep& step : LoadSteps.Failures())
            failed.emplace(step.Name, step.Reason);
        return Json::Write(
            glz::obj{"steps", LoadSteps.Count(), "failed", failed, "unavailable", UnavailableFeatures()});
    });

    Status.RegisterSection("uptime", [start = std::chrono::steady_clock::now()] {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return Json::Write(glz::obj{"seconds", uptime.count()});
    });
}

void Runtime::OnGameFrame()
{
    Log::DeliverPending();
    Scheduler.OnGameFrame();
}

}  // namespace VoltMod
