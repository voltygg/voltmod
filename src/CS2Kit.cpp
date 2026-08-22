#include "Core/ConsoleLogger.hpp"
#include "Sdk/Schema.hpp"

#include <CS2Kit/CS2Kit.hpp>
#include <CS2Kit/Core/ILogger.hpp>
#include <CS2Kit/Core/Paths.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/Services.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/ClientCvarService.hpp>
#include <CS2Kit/Sdk/ConVarService.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/PrecacheService.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <CS2Kit/Utils/Log.hpp>
#include <ISmmAPI.h>
#include <chrono>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <networksystem/inetworkmessages.h>
#include <nlohmann/json.hpp>
#include <schemasystem/schemasystem.h>
#include <string_view>
#include <tier1/convar.h>

namespace CS2Kit
{

static constexpr const char* DefaultGameDataPath = "addons/cs2-kit/gamedata/signatures.jsonc";
static Core::ConsoleLogger g_consoleLogger;

bool Initialize(ISmmAPI* ismm, char* error, size_t maxlen, Core::Services& services, const InitParams& params)
{
    // 1. Set up logging
    if (params.Logger)
    {
        Core::SetGlobalLogger(params.Logger);
    }
    else
    {
        g_consoleLogger.SetPrefix(params.LogPrefix);
        Core::SetGlobalLogger(&g_consoleLogger);
    }

    // 2. Set base directory for path resolution
    Core::SetBaseDir(ismm->GetBaseDir());

    Utils::Log::Info("Initializing CS2Kit...");

    // 3. Resolve SDK interfaces via Metamod
    auto resolveEngine = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetEngineFactory(), version, 0);
    };
    auto resolveServer = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetServerFactory(), version, 0);
    };

    auto& gi = services.Interfaces;

    // Resolve each required interface, erroring out on the first one that is missing. The macro
    // keeps this type-safe (decltype, no void** punning) while collapsing the per-interface
    // resolve-and-check boilerplate to one line each.
#define CS2KIT_RESOLVE(field, factory, version)                               \
    gi.field = static_cast<decltype(gi.field)>(factory(version));             \
    if (!gi.field)                                                            \
    {                                                                         \
        ismm->Format(error, maxlen, "Could not find interface: %s", version); \
        return false;                                                         \
    }

    CS2KIT_RESOLVE(ServerGameDLL, resolveServer, INTERFACEVERSION_SERVERGAMEDLL)
    CS2KIT_RESOLVE(ServerGameClients, resolveServer, INTERFACEVERSION_SERVERGAMECLIENTS)
    CS2KIT_RESOLVE(NetworkServerService, resolveEngine, NETWORKSERVERSERVICE_INTERFACE_VERSION)
    CS2KIT_RESOLVE(GameEntities, resolveServer, INTERFACEVERSION_SERVERGAMEENTS)
    CS2KIT_RESOLVE(Engine, resolveEngine, INTERFACEVERSION_VENGINESERVER)
    CS2KIT_RESOLVE(GameEventSystem, resolveEngine, GAMEEVENTSYSTEM_INTERFACE_VERSION)
    CS2KIT_RESOLVE(NetworkMessages, resolveEngine, NETWORKMESSAGES_INTERFACE_VERSION)
    CS2KIT_RESOLVE(SchemaSystem, resolveEngine, SCHEMASYSTEM_INTERFACE_VERSION)
    CS2KIT_RESOLVE(CVar, resolveEngine, CVAR_INTERFACE_VERSION)
    CS2KIT_RESOLVE(GameResourceService, resolveEngine, GAMERESOURCESERVICESERVER_INTERFACE_VERSION)

#undef CS2KIT_RESOLVE

    // 4. Set g_pCVar and flush pending registrations; without ConVar_Register, tier1 ConCommands
    // (CS2Kit::ServerCommand) construct but never register, so the engine reports "Unknown command".
    g_pCVar = gi.CVar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);

    // 5-6. Load game data and initialize SDK subsystems as named, timed stages.
    // Only the message system is load-aborting; MetamodPluginBase logs the
    // summary and surfaces FirstFailure() in Metamod's error buffer.
    using Core::StageResult;
    auto& report = services.LoadReport;

    report.Run("GameData", [&] {
        const char* gameDataPath = params.GameDataPath ? params.GameDataPath : DefaultGameDataPath;
        if (!services.GameData.Load(gameDataPath))
            return StageResult::Degraded(std::format("failed to load {}", gameDataPath));
        services.GameData.ResolveAll();
        if (auto failures = services.GameData.FailureSummary(); !failures.empty())
            return StageResult::Degraded(std::move(failures));
        return StageResult::Ok(std::format("{} offsets, {} signatures resolved", services.GameData.OffsetCount(),
                                           services.GameData.SignatureCount()));
    });

    const auto messages = report.Run("Messages", [&] {
        if (!services.Messages.Initialize())
            return StageResult::Failed("message system init failed");
        return StageResult::Ok();
    });
    if (messages == Core::StageStatus::Failed)
    {
        ismm->Format(error, maxlen, "%s", report.FirstFailure().c_str());
        return false;
    }

    // The remaining subsystems all report the same way: a false return degrades the load
    // with `detail` and the plugin keeps going without that capability.
    auto degradable = [&report](std::string_view name, std::string detail, auto&& init) {
        report.Run(name, [&] { return init() ? StageResult::Ok() : StageResult::Degraded(std::move(detail)); });
    };

    degradable("Schema", "init failed; button detection may not work", [&] { return services.Schema().Initialize(); });
    degradable("Entities", "init failed; menus may not work", [&] { return services.Entities.Initialize(); });
    degradable("EntityOps", "unavailable; spawned effects degrade (see signature warnings)",
               [&] { return services.EntityOps.Initialize(); });
    degradable("Precache", "not registered; resource precaching unavailable",
               [&] { return services.Precache.Initialize(std::format("{}_CS2KitPrecache", params.LogPrefix)); });
    degradable("GameEventManager", "not resolved; center HTML display will not work",
               [&] { return services.Messages.InitGameEventManager(); });
    degradable("ConVars", "init failed", [&] { return services.ConVars.Initialize(); });
    degradable("Events", "init failed", [&] { return services.Events.Initialize(); });
    degradable("Transmit", "inert; CheckTransmitPlayerSlot offset missing from gamedata",
               [&] { return services.Transmit.Initialize(); });
    degradable("ClientCvars", "inert; client convar queries unavailable (see warnings)",
               [&] { return services.ClientCvars.Initialize(); });

    // Per-frame subsystems pump through the scheduler (PostgresDatabase registers its own pump
    // in Start), so OnGameFrame has exactly one thing to tick. CancelAll in Shutdown unhooks
    // these; Initialize re-registers them on the next load.
    services.Scheduler.EveryFrame([&services] { services.Menus.OnGameFrame(); });
    services.Scheduler.EveryFrame([&services] { services.Http.DispatchCompletions(); });

    // Kit status sections; plugins add theirs in OnLoad. Providers capture `services` by
    // reference - it outlives them (both live for one Load/Unload cycle).
    services.Status.RegisterSection("load", [&services] {
        auto names = nlohmann::json::object();
        int ok = 0;
        for (const auto& stage : services.LoadReport.Stages())
        {
            if (stage.Status == Core::StageStatus::Ok)
                ++ok;
            else
                names[std::string(Core::ToString(stage.Status))].push_back(stage.Name);
        }
        names["ok"] = ok;
        return names;
    });

    services.Status.RegisterSection("gamedata", [&services] {
        auto section = nlohmann::json{{"offsets", services.GameData.OffsetCount()},
                                      {"signatures", services.GameData.SignatureCount()}};
        for (const auto& [name, entry] : services.GameData.Resolutions())
        {
            if (!entry.Error.empty())
                section["failed"].push_back(name);
            else if (!entry.Unique)
                section["ambiguous"].push_back(name);
        }
        return section;
    });

    services.Status.RegisterSection("uptime", [start = std::chrono::steady_clock::now()] {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return nlohmann::json{{"seconds", uptime.count()}};
    });

    return true;
}

void Shutdown(Core::Services& services)
{
    // First: peers must stop resolving our interfaces while their objects are still alive.
    services.Identity.Withdraw();
    services.Precache.Shutdown();  // first: the engine must stop referencing our vtables
    services.ClientCvars.Shutdown();
    services.Events.RemoveAllListeners();
    services.Http.Stop();  // drains in-flight requests before their completion targets go away
    services.Scheduler.CancelAll();
}

void OnGameFrame(Core::Services& services)
{
    services.Scheduler.OnGameFrame();
}

void OnPlayerDisconnect(Core::Services& services, int slot)
{
    services.Menus.OnPlayerDisconnect(slot);
    services.ChatInput.OnPlayerDisconnect(slot);
    services.Transmit.OnPlayerDisconnect(slot);
    services.ClientCvars.OnPlayerDisconnect(slot);
}

}  // namespace CS2Kit
