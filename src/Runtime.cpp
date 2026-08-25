#include "Core/ConsoleLogger.hpp"
#include "Sdk/Internal/Schema.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Detail/Runtime.hpp>
#include <VoltMod/Runtime.hpp>
#include <chrono>
#include <cstdlib>
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

namespace VoltMod
{

namespace
{
constexpr const char* DefaultGameDataPath = "addons/voltmod/gamedata/signatures.jsonc";
Core::ConsoleLogger g_consoleLogger;
Runtime* g_active = nullptr;
}  // namespace

namespace Detail
{

void SetRt(Runtime* runtime)
{
    g_active = runtime;
}

Runtime& Rt()
{
    if (!g_active)
    {
        // Release builds would otherwise take a null dereference into a Metamod crash dump
        // with no context. Name the mistake instead: this is always a lifetime bug.
        Core::Log::Error("VoltMod::Detail::Rt() called with no live Runtime (outside Load/Unload).");
        std::abort();
    }
    return *g_active;
}

Runtime* RtOrNull()
{
    return g_active;
}

Core::SlotEvents& Slots()
{
    return Rt().Slots;
}

Core::Translations& Translations()
{
    return Rt().Translations;
}

Core::PluginPolicy& Policy()
{
    return Rt().Policy;
}

Menu::MenuManager& Menus()
{
    return Rt().Menus;
}

}  // namespace Detail

Runtime::Runtime() : _schema(std::make_unique<Sdk::SchemaService>()) {}

Runtime::~Runtime()
{
    // First: peers must stop resolving our interfaces while their objects are still alive.
    Identity.Withdraw();
    Precache.Shutdown();  // the engine must stop referencing our vtables
    ClientCvars.Shutdown();
    ConVars.Shutdown();
    Events.RemoveAllListeners();
    Http.Stop();  // drains in-flight requests before their completion targets go away
    Scheduler.CancelAll();
    // The workers have joined by now; their last lines are still queued and OnGameFrame will
    // not run again.
    Core::DrainDeferredLogs();
}

bool Runtime::Start(const LoadContext& context)
{
    InstallLogger(context);
    Core::Log::Info("Initializing VoltMod...");

    if (!ResolveInterfaces(context))
        return false;

    if (!InitializeServices(context))
        return false;

    RegisterStatusSections();
    return true;
}

void Runtime::InstallLogger(const LoadContext& context)
{
    if (context.Logger)
    {
        Core::SetGlobalLogger(context.Logger);
    }
    else
    {
        g_consoleLogger.SetPrefix(context.LogPrefix);
        Core::SetGlobalLogger(&g_consoleLogger);
    }

    Core::SetBaseDir(context.Ismm->GetBaseDir());
}

bool Runtime::ResolveInterfaces(const LoadContext& context)
{
    ISmmAPI* ismm = context.Ismm;

    auto resolveEngine = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetEngineFactory(), version, 0);
    };
    auto resolveServer = [&](const char* version) -> void* {
        return ismm->VInterfaceMatch(ismm->GetServerFactory(), version, 0);
    };

    auto& gi = Interfaces;

    // Resolve each required interface, erroring out on the first one that is missing. The macro
    // keeps this type-safe (decltype, no void** punning) while collapsing the per-interface
    // resolve-and-check boilerplate to one line each.
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

    // Set g_pCVar and flush pending registrations; without ConVar_Register, tier1 ConCommands
    // (VoltMod::ServerCommand) construct but never register, so the engine reports "Unknown command".
    g_pCVar = gi.CVar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    return true;
}

bool Runtime::InitializeServices(const LoadContext& context)
{
    // Load game data and initialize SDK subsystems as named, timed stages. Only the message
    // system is load-aborting; MetamodPlugin logs the summary and surfaces FirstFailure() in
    // Metamod's error buffer.
    using Core::StageResult;
    auto& report = LoadReport;

    report.Run("GameData", [&] {
        const char* gameDataPath = context.GameDataPath ? context.GameDataPath : DefaultGameDataPath;
        if (!GameData.Load(gameDataPath))
            return StageResult::Degraded(std::format("failed to load {}", gameDataPath));
        GameData.ResolveAll();
        if (auto failures = GameData.FailureSummary(); !failures.empty())
            return StageResult::Degraded(std::move(failures));
        return StageResult::Ok(
            std::format("{} offsets, {} signatures resolved", GameData.OffsetCount(), GameData.SignatureCount()));
    });

    const auto messages = report.Run("Messages", [&] {
        if (!Messages.Initialize())
            return StageResult::Failed("message system init failed");
        return StageResult::Ok();
    });
    if (messages == Core::StageStatus::Failed)
    {
        context.Ismm->Format(context.Error, context.MaxLen, "%s", report.FirstFailure().c_str());
        return false;
    }

    // The remaining subsystems all report the same way: a false return degrades the load
    // with `detail` and the plugin keeps going without that capability.
    auto degradable = [&report](std::string_view name, std::string detail, auto&& init) {
        report.Run(name, [&] { return init() ? StageResult::Ok() : StageResult::Degraded(std::move(detail)); });
    };

    degradable("Schema", "init failed; button detection may not work", [&] { return Schema().Initialize(); });
    degradable("Entities", "init failed; menus may not work", [&] { return Entities.Initialize(); });
    degradable("EntityOps", "unavailable; spawned effects degrade (see signature warnings)",
               [&] { return EntityOps.Initialize(); });
    degradable("Precache", "not registered; resource precaching unavailable",
               [&] { return Precache.Initialize(std::format("{}_VoltModPrecache", context.LogPrefix)); });
    degradable("GameEventManager", "not resolved; center HTML display will not work",
               [&] { return Messages.InitGameEventManager(); });
    degradable("ConVars", "init failed", [&] { return ConVars.Initialize(); });
    degradable("Events", "init failed", [&] { return Events.Initialize(); });
    degradable("Transmit", "inert; CheckTransmitPlayerSlot offset missing from gamedata",
               [&] { return Transmit.Initialize(); });
    degradable("ClientCvars", "inert; client convar queries unavailable (see warnings)",
               [&] { return ClientCvars.Initialize(); });

    return true;
}

void Runtime::RegisterStatusSections()
{
    // Framework status sections; plugins add theirs in OnLoad. Providers capture `this` - the
    // runtime outlives them (both live for one Load/Unload cycle).
    Status.RegisterSection("load", [this] {
        auto names = nlohmann::json::object();
        int ok = 0;
        for (const auto& stage : LoadReport.Stages())
        {
            if (stage.Status == Core::StageStatus::Ok)
                ++ok;
            else
                names[std::string(Core::ToString(stage.Status))].push_back(stage.Name);
        }
        names["ok"] = ok;
        return names;
    });

    Status.RegisterSection("gamedata", [this] {
        auto section = nlohmann::json{{"offsets", GameData.OffsetCount()}, {"signatures", GameData.SignatureCount()}};
        for (const auto& [name, entry] : GameData.Resolutions())
        {
            if (!entry.Error.empty())
                section["failed"].push_back(name);
            else if (!entry.Unique)
                section["ambiguous"].push_back(name);
        }
        return section;
    });

    Status.RegisterSection("uptime", [start = std::chrono::steady_clock::now()] {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return nlohmann::json{{"seconds", uptime.count()}};
    });
}

void Runtime::OnGameFrame()
{
    Core::DrainDeferredLogs();
    Scheduler.OnGameFrame();
}

}  // namespace VoltMod
