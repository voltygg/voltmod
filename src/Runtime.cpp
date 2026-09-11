#include "Engine/Server/ConsoleLogger.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Runtime.hpp>
#include <VoltMod/Schema/Layout.hpp>
#include <chrono>
#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <format>
#include <icvar.h>
#include <interfaces/interfaces.h>
#include <map>
#include <networksystem/inetworkmessages.h>
#include <optional>
#include <schemasystem/schemasystem.h>
#include <string>
#include <string_view>
#include <tier1/convar.h>
#include <vector>

namespace VoltMod
{

static constexpr std::string_view DefaultGameDataPath = "addons/voltmod/gamedata/gamedata.jsonc";

// Member initializers wire services in dependency order.
Runtime::Runtime() = default;

// Service destructors stop them in reverse declaration order.
Runtime::~Runtime()
{
    // Stop HTTP workers and deliver queued logs before OnGameFrame stops with hook removal.
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

    // Resolve interfaces in order. decltype keeps assignments type-safe without void** casts.
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

    // Register pending tier1 ConCommands so the engine knows VoltMod::ServerCommand instances.
    g_pCVar = gi.CVar;
    ConVar_Register(FCVAR_RELEASE | FCVAR_CLIENT_CAN_EXECUTE | FCVAR_SERVER_CAN_EXECUTE | FCVAR_GAMEDLL);
    return true;
}

bool Runtime::InitializeServices(const LoadContext& context)
{
    // Run named, timed stages. MetamodPlugin logs the summary and reports the first fatal failure.
    auto& report = LoadReport;

    report.Run("GameData", [&] {
        // Earlier plugins may patch class tables. Resolve the original slot through SourceHook.
        if (auto loaded = Unsafe.GameData.Load(DefaultGameDataPath, OriginalVfnPtr); !loaded)
            return StageResult::Degraded(loaded.error().Detail);
        if (auto failures = Unsafe.GameData.FailureSummary(); !failures.empty())
            return StageResult::Degraded(std::move(failures));
        return StageResult::Ok(std::format("{} entries resolved (verified {})", Unsafe.GameData.Resolutions().size(),
                                           Unsafe.GameData.VerifiedOn()));
    });

    // Run after degraded GameData so disabled capabilities keep their reasons.
    report.Run("Bindings", [&] {
        if (auto bound = Unsafe.Bindings.Bind(Unsafe.GameData, Capabilities); !bound)
            return StageResult::Degraded(bound.error().Detail);
        return StageResult::Ok();
    });

    // Fatal stages write the first failure to Metamod and abort the load.
    auto fatal = [&](std::string_view name, auto&& init) {
        const auto status = report.Run(name, [&] {
            auto ready = init();
            return ready ? StageResult::Ok() : StageResult::Failed(ready.error().Detail);
        });
        if (status != StageStatus::Failed)
            return true;

        context.Ismm->Format(context.Error, context.MaxLen, "%s", report.FirstFailure().c_str());
        return false;
    };

    if (!fatal("Messages", [&] { return Messages.Initialize(); }))
        return false;

    // Degradable stages keep the load alive and record the failure on their capability.
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

    // Abort when the live CS2 layout differs from generated offsets.
    Schema::BindSchemaVerification(Unsafe.Interfaces.SchemaSystem);
    if (!fatal("SchemaLayout", [&] { return Schema::VerifySchemaLayout(); }))
    {
        return false;
    }

    report.Run("Entities", [&] {
        auto ready = Entities.Initialize();
        if (!ready)
        {
            Capabilities.Set(Capability::Entities, false, ready.error().Detail);
            return StageResult::Degraded(ready.error().Detail);
        }
        if (Entities.GetEntitySystem())
            return StageResult::Ok();

        // StartupServer resolves CGameEntitySystem if load precedes engine creation.
        return StageResult::Ok("resolves at the first map load");
    });

    // Bindings already determines EntityOps and Visibility; report those results here.
    auto alreadyDecided = [&](std::string_view name, Capability capability) {
        report.Run(name, [&] {
            return Capabilities.Has(capability) ? StageResult::Ok()
                                                : StageResult::Degraded(std::string(Capabilities.Reason(capability)));
        });
    };
    alreadyDecided("EntityOps", Capability::EntityOps);
    alreadyDecided("Visibility", Capability::Visibility);
    degradable("Precache", Capability::Precache,
               [&] { return World.Precache.Initialize(std::format("{}_VoltModPrecache", context.LogPrefix)); });
    degradable("GameEventManager", Capability::GameEvents, [&] { return Messages.InitGameEventManager(); });
    report.Run("ConVars", [&] {
        if (auto ready = ConVars.Initialize(); !ready)
            return StageResult::Degraded(ready.error().Detail);
        return StageResult::Ok();
    });
    degradable("GameEvents", Capability::GameEvents, [&] { return GameEvents.Initialize(); });
    degradable("ClientConVars", Capability::ClientConVars, [&] { return Hooks.ClientConVars.Initialize(); });

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
    // Plugins add status sections in OnLoad. The runtime outlives them for the load cycle.
    // Serialize JSON values directly because capability reasons are free text.
    Status.RegisterSection("load", [this] {
        std::map<StageStatus, std::vector<std::string>> byStatus;
        int ok = 0;
        for (const auto& stage : LoadReport.Stages())
        {
            if (stage.Status == StageStatus::Ok)
                ++ok;
            else
                byStatus[stage.Status].push_back(stage.Name);
        }

        std::map<std::string, glz::raw_json> section;
        for (const auto& [status, names] : byStatus)
            section[std::string(Name(status))] = glz::raw_json{Json::Write(names)};
        section["ok"] = glz::raw_json{std::to_string(ok)};
        return Json::Write(section);
    });

    Status.RegisterSection("gamedata", [this] {
        std::optional<std::vector<std::string>> failed;
        for (const auto& [name, entry] : Unsafe.GameData.Resolutions())
        {
            if (!entry.Error.empty())
            {
                if (!failed)
                    failed.emplace();
                failed->push_back(name);
            }
        }
        return Json::Write(glz::obj{"verified", Unsafe.GameData.VerifiedOn(), "signatures",
                                    Unsafe.GameData.CountOf(GameData::Kind::Signature), "addresses",
                                    Unsafe.GameData.CountOf(GameData::Kind::Address), "vtables",
                                    Unsafe.GameData.CountOf(GameData::Kind::VTable), "offsets",
                                    Unsafe.GameData.CountOf(GameData::Kind::Offset), "failed", failed});
    });

    Status.RegisterSection("capabilities", [this] {
        std::optional<std::map<std::string, std::string>> missing;
        int ok = 0;
        for (Capability capability : EnumValues<Capability>())
        {
            if (Capabilities.Has(capability))
                ++ok;
            else
            {
                if (!missing)
                    missing.emplace();
                missing->emplace(std::string(Name(capability)), std::string(Capabilities.Reason(capability)));
            }
        }
        return Json::Write(glz::obj{"missing", missing, "ok", ok});
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
