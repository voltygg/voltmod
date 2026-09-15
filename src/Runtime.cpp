#include "Engine/Server/ConsoleLogger.hpp"
#include "Schema/Layout.hpp"

#include <ISmmAPI.h>
#include <VoltMod/Core/EnumNames.hpp>
#include <VoltMod/Core/Json.hpp>
#include <VoltMod/Core/Log.hpp>
#include <VoltMod/Core/Paths.hpp>
#include <VoltMod/Engine/MetamodGlobals.hpp>
#include <VoltMod/Runtime.hpp>
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
    // MetamodPlugin logs the summary and reports why a required step failed.
    auto& steps = LoadSteps;

    steps.Optional("GameData", [&]() -> VoltMod::Status {
        // Earlier plugins may patch class tables. Resolve the original slot through KHook.
        if (auto loaded = Unsafe.GameData.Load(DefaultGameDataPath, OriginalVfnPtr); !loaded)
            return loaded;
        if (auto failures = Unsafe.GameData.FailureSummary(); !failures.empty())
            return std::unexpected(Error::Engine(std::move(failures)));
        return {};
    });

    // Run after degraded GameData so disabled capabilities keep their reasons.
    steps.Optional("Bindings", [&] { return Unsafe.Bindings.Bind(Unsafe.GameData, Capabilities); });

    // A required step writes its failure to Metamod and aborts the load.
    auto requiredStep = [&](std::string_view name, const std::function<VoltMod::Status()>& step) {
        if (steps.Required(name, step))
            return true;

        context.Ismm->Format(context.Error, context.MaxLen, "%s", steps.AbortReason().c_str());
        return false;
    };

    if (!requiredStep("Messages", [&] { return Messages.Initialize(); }))
        return false;

    // An optional step keeps the load alive and records the failure on its capability.
    auto optionalStep = [&](std::string_view name, Capability capability,
                            const std::function<VoltMod::Status()>& step) {
        steps.Optional(name, [&] {
            VoltMod::Status ready = step();
            Capabilities.Set(capability, ready.has_value(), ready ? std::string() : ready.error().Detail);
            return ready;
        });
    };

    // Abort on schema drift. A load into a running map writes the dump first, so a refusal still leaves one.
    if (!requiredStep("SchemaLayout", [&] {
            Schema::WriteSchemaDump(Unsafe.Interfaces.SchemaSystem, Entities.GetEntitySystem());
            return Schema::VerifySchemaLayout(Unsafe.Interfaces.SchemaSystem);
        }))
    {
        return false;
    }

    // Without an entity system yet, StartupServer resolves CGameEntitySystem at the first map load.
    steps.Optional("Entities", [&] {
        VoltMod::Status ready = Entities.Initialize();
        if (!ready)
            Capabilities.Set(Capability::Entities, false, ready.error().Detail);
        return ready;
    });

    optionalStep("Precache", Capability::Precache,
                 [&] { return World.Precache.Initialize(std::format("{}_VoltModPrecache", context.LogPrefix)); });
    steps.Optional("ConVars", [&] { return ConVars.Initialize(); });
    optionalStep("GameEvents", Capability::GameEvents, [&] { return GameEvents.Initialize(); });
    optionalStep("ClientConVars", Capability::ClientConVars, [&] { return Hooks.ClientConVars.Initialize(); });

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
        std::map<std::string, std::string> failed;
        for (const FailedStep& step : LoadSteps.Failures())
            failed.emplace(step.Name, step.Reason);
        return Json::Write(glz::obj{"steps", LoadSteps.Count(), "failed", failed});
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
