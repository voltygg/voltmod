#include "Engine/ConsoleLogger.hpp"
#include "Entities/SchemaResolve.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Runtime.hpp>
#include <chrono>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <networksystem/inetworkmessages.h>
#include <schemasystem/schemasystem.h>
#include <string_view>
#include <tier1/convar.h>

namespace VoltMod
{

static constexpr std::string_view DefaultGameDataPath = "addons/voltmod/gamedata/gamedata.jsonc";

// Every service is wired by its default member initializer in Runtime.hpp, where the dependency
// order is visible.
Runtime::Runtime() = default;

// Every service tears itself down in its own destructor, in reverse declaration order. Only these
// two cannot wait for that.
Runtime::~Runtime()
{
    // Both must precede member destruction: Http.Stop() joins the workers, whose final log lines
    // are queued for the game thread, and OnGameFrame never runs again once the hooks are gone -
    // so this second delivery is the only thing that keeps shutdown diagnostics from being dropped.
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
    SetBaseDir(context.Ismm->GetBaseDir());
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

    auto& gi = Unsafe.Interfaces;

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
    auto& report = LoadReport;

    report.Run("GameData", [&] {
        if (auto loaded = Unsafe.GameData.Load(DefaultGameDataPath); !loaded)
            return StageResult::Degraded(loaded.error().Detail);
        if (auto failures = Unsafe.GameData.FailureSummary(); !failures.empty())
            return StageResult::Degraded(std::move(failures));
        return StageResult::Ok(std::format("{} entries resolved (verified {})", Unsafe.GameData.Resolutions().size(),
                                           Unsafe.GameData.VerifiedOn()));
    });

    // Bindings must run even when GameData degraded: it is what records every capability, and a
    // capability nobody records reads as off with "not initialized" rather than with the reason.
    report.Run("Bindings", [&] {
        if (auto bound = Unsafe.Bindings.Bind(Unsafe.GameData, Capabilities); !bound)
            return StageResult::Degraded(bound.error().Detail);
        return StageResult::Ok();
    });

    const auto messages = report.Run("Messages", [&] {
        if (auto ready = Messages.Initialize(); !ready)
            return StageResult::Failed(ready.error().Detail);
        return StageResult::Ok();
    });
    if (messages == StageStatus::Failed)
    {
        context.Ismm->Format(context.Error, context.MaxLen, "%s", report.FirstFailure().c_str());
        return false;
    }

    // The remaining subsystems all report the same way: the setup's error degrades the load stage
    // and turns off the capability it backs, with the same reason on both.
    auto degradable = [&](std::string_view name, Capability capability, auto&& init) {
        report.Run(name, [&] {
            auto ready = init();
            if (!ready)
            {
                Capabilities.Set(capability, false, ready.error().Detail);
                return StageResult::Degraded(ready.error().Detail);
            }
            Capabilities.Set(capability, true);
            return StageResult::Ok();
        });
    };

    // One process-wide file-static, not a per-Runtime service: the schema system is a single
    // engine object and the offsets it answers with are constants of the loaded binary.
    degradable("Schema", Capability::Schema, [&] { return BindSchemaSystem(Unsafe.Interfaces.SchemaSystem); });
    report.Run("Entities", [&] {
        auto ready = Entities.Initialize();
        if (!ready)
        {
            Capabilities.Set(Capability::Entities, false, ready.error().Detail);
            return StageResult::Degraded(ready.error().Detail);
        }
        if (Entities.GetEntitySystem())
            return StageResult::Ok();

        // A load that runs before the engine creates CGameEntitySystem; StartupServer resolves it.
        return StageResult::Ok("resolves at the first map load");
    });
    // EntityOps and Transmit have no setup of their own: Bindings already decided both, so the
    // stage only reports what the capability already says.
    auto alreadyDecided = [&](std::string_view name, Capability capability) {
        report.Run(name, [&] {
            return Capabilities.Has(capability) ? StageResult::Ok()
                                                : StageResult::Degraded(std::string(Capabilities.Reason(capability)));
        });
    };
    alreadyDecided("EntityOps", Capability::EntityOps);
    alreadyDecided("Transmit", Capability::Transmit);
    degradable("Precache", Capability::Precache,
               [&] { return World.Precache.Initialize(std::format("{}_VoltModPrecache", context.LogPrefix)); });
    degradable("GameEventManager", Capability::GameEvents, [&] { return Messages.InitGameEventManager(); });
    report.Run("ConVars", [&] {
        if (auto ready = ConVars.Initialize(); !ready)
            return StageResult::Degraded(ready.error().Detail);
        return StageResult::Ok();
    });
    degradable("GameEvents", Capability::GameEvents, [&] { return GameEvents.Initialize(); });
    degradable("ClientCvars", Capability::ClientCvars, [&] { return Hooks.ClientCvars.Initialize(); });

    // The last four have no gamedata or engine setup to fail: Vote and Menus ride on the services
    // above, and Http owns its own worker pool.
    Capabilities.Set(Capability::Vote,
                     Capabilities.Has(Capability::GameEvents) && Capabilities.Has(Capability::Entities),
                     "needs GameEvents and Entities");
    Capabilities.Set(Capability::Menus, Capabilities.Has(Capability::Entities), "needs Entities");
    Capabilities.Set(Capability::Http, true);

    Log::Info("Capabilities: {}", Capabilities.Summary());
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
            if (stage.Status == StageStatus::Ok)
                ++ok;
            else
                names[std::string(Name(stage.Status))].push_back(stage.Name);
        }
        names["ok"] = ok;
        return names.dump();
    });

    Status.RegisterSection("gamedata", [this] {
        auto section = nlohmann::json{{"verified", Unsafe.GameData.VerifiedOn()},
                                      {"signatures", Unsafe.GameData.CountOf(GameData::Kind::Signature)},
                                      {"addresses", Unsafe.GameData.CountOf(GameData::Kind::Address)},
                                      {"vtables", Unsafe.GameData.CountOf(GameData::Kind::VTable)},
                                      {"offsets", Unsafe.GameData.CountOf(GameData::Kind::Offset)}};
        for (const auto& [name, entry] : Unsafe.GameData.Resolutions())
        {
            if (!entry.Error.empty())
                section["failed"].push_back(name);
        }
        return section.dump();
    });

    Status.RegisterSection("capabilities", [this] {
        auto section = nlohmann::json::object();
        int ok = 0;
        for (Capability capability : EnumValues<Capability>())
        {
            if (Capabilities.Has(capability))
                ++ok;
            else
                section["missing"][std::string(Name(capability))] = Capabilities.Reason(capability);
        }
        section["ok"] = ok;
        return section.dump();
    });

    Status.RegisterSection("uptime", [start = std::chrono::steady_clock::now()] {
        const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        return nlohmann::json{{"seconds", uptime.count()}}.dump();
    });
}

void Runtime::OnGameFrame()
{
    Log::DeliverPending();
    Scheduler.OnGameFrame();
}

}  // namespace VoltMod
