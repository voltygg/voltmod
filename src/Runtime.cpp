#include "Engine/Server/ConsoleLogger.hpp"
#include "Schema/Layout.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/Text/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Files/Paths.hpp>
#include <VoltMod/Engine/Detours.hpp>
#include <VoltMod/Runtime.hpp>
#include <chrono>
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

static constexpr std::string_view DefaultGameDataPath = "addons/voltmod/gamedata/gamedata.jsonc";

Runtime::Runtime() = default;

Runtime::~Runtime()
{
    // Stop HTTP workers and flush queued logs before removing frame hooks.
    Http.Stop();
    Log::DeliverPending();
}

bool Runtime::Start(const LoadContext& context)
{
    InstallLogger(context);
    Log::Info("Initializing VoltMod...");
    Log::Info("Runtime size: {} bytes", sizeof(Runtime));

    if (!ResolveInterfaces(context))
        return false;

    if (!InitializeServices(context))
        return false;

    RegisterStatusSections();
    return true;
}

void Runtime::InstallLogger(const LoadContext& context)
{
    Log::SetHandler(MakeConsoleHandler(std::string(context.LogPrefix)));
    SetBaseDir(context.Host->Metamod()->GetBaseDir());
}

bool Runtime::ResolveInterfaces(const LoadContext& context)
{
    // The host is the Metamod plugin and shares its own pointer, so this resolves exactly as it
    // did when every plugin was one.
    ISmmAPI* ismm = context.Host->Metamod();

    auto resolveEngine = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetEngineFactory(), version, 0);
    };
    auto resolveServer = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetServerFactory(), version, 0);
    };

    auto& gi = Unsafe.Interfaces;

    // Resolve interfaces in order without unsafe void** casts.
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

    // Log unbound entries. Earlier plugins may hook class tables, so read slots through KHook.
    steps.Optional("GameData", [&] { return Unsafe.Bindings.Load(DefaultGameDataPath, ReadOriginalSlot); });

    // A required-step failure is reported to Metamod and aborts the load.
    auto requiredStep = [&](std::string_view name, const std::function<VoltMod::Status()>& step) {
        if (steps.Required(name, step))
            return true;

        context.Host->Metamod()->Format(context.Error, context.MaxLen, "%s", steps.AbortReason().c_str());
        return false;
    };

    if (!requiredStep("Messages", [&] { return Messages.Initialize(); }))
        return false;

    // Abort on schema drift. A running map writes the dump before refusing the load.
    if (!requiredStep("SchemaLayout", [&] {
            Schema::WriteSchemaDump(Unsafe.Interfaces.SchemaSystem, Entities.GetEntitySystem());
            return Schema::VerifySchemaLayout(Unsafe.Interfaces.SchemaSystem);
        }))
    {
        return false;
    }

    // StartupServer resolves CGameEntitySystem when the first map loads.
    steps.Optional("Entities", [&] { return Entities.Initialize(); });
    steps.Optional("Precache",
                   [&] { return World.Precache.Initialize(std::format("{}_VoltModPrecache", context.LogPrefix)); });
    steps.Optional("ConVars", [&] { return ConVars.Initialize(); });
    steps.Optional("GameEvents", [&] { return GameEvents.Initialize(); });
    steps.Optional("ClientConVars", [&] { return Hooks.ClientConVars.Initialize(); });

    for (const auto& [feature, reason] : UnavailableFeatures())
        Log::Warn("{} is unavailable: {}", feature, reason);
    return true;
}

std::map<std::string, std::string> Runtime::UnavailableFeatures() const
{
    const std::pair<std::string_view, VoltMod::Status> features[] = {
        {"Movement", Hooks.Movement.Available()},
        {"Teleport", Hooks.Teleport.Available()},
        {"Visibility", Hooks.Visibility.Available()},
        {"Trace", World.Trace.Available()},
        {"ClientConVars", Hooks.ClientConVars.Available()},
        {"Screens", Screens.Available()},
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
    // Plugins add status sections during OnLoad; the runtime owns them for the load cycle.
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
