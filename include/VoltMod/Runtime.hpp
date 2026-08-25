#pragma once

#include <VoltMod/App/PluginIdentity.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/ILogger.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/PluginPolicy.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Sdk/Client/ClientCvarService.hpp>
#include <VoltMod/Sdk/Engine/ConVarService.hpp>
#include <VoltMod/Sdk/Engine/GameData.hpp>
#include <VoltMod/Sdk/Engine/GameInterfaces.hpp>
#include <VoltMod/Sdk/Engine/MapService.hpp>
#include <VoltMod/Sdk/Engine/NetChannel.hpp>
#include <VoltMod/Sdk/Engine/PrecacheService.hpp>
#include <VoltMod/Sdk/Engine/ServerClock.hpp>
#include <VoltMod/Sdk/Entity/DamageHook.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/EntityOps.hpp>
#include <VoltMod/Sdk/Entity/ItemService.hpp>
#include <VoltMod/Sdk/Entity/PawnService.hpp>
#include <VoltMod/Sdk/Events/GameEventService.hpp>
#include <VoltMod/Sdk/Messaging/ChatInputCapture.hpp>
#include <VoltMod/Sdk/Messaging/PanoramaVote.hpp>
#include <VoltMod/Sdk/Messaging/UserMessage.hpp>
#include <VoltMod/Sdk/Movement/InputHistoryService.hpp>
#include <VoltMod/Sdk/Movement/MovementHook.hpp>
#include <VoltMod/Sdk/Movement/TeleportTracker.hpp>
#include <VoltMod/Sdk/Visibility/TransmitFilter.hpp>
#include <VoltMod/Sdk/Visibility/VisibilityService.hpp>
#include <cstddef>
#include <memory>
#include <string>

namespace SourceMM
{
class ISmmAPI;
}
using SourceMM::ISmmAPI;

namespace VoltMod::Sdk
{
class SchemaService;  // Internal type kept out of the public include graph.
}

namespace VoltMod
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    ISmmAPI* Ismm = nullptr;             ///< Metamod API pointer, from Plugin::Load
    char* Error = nullptr;               ///< Error buffer Metamod shows if the load fails
    size_t MaxLen = 0;                   ///< Size of that buffer
    bool Late = false;                   ///< Loaded after the server had already started
    const char* LogPrefix = "VoltMod";   ///< Console log prefix, e.g. "[ADMIN]"
    const char* GameDataPath = nullptr;  ///< Override signatures.jsonc; null uses the shipped one
    Core::ILogger* Logger = nullptr;     ///< Custom logger; null uses the console logger
};

/**
 * @brief Framework services for one Load/Unload cycle.
 *
 * Services are flat so their public names do not depend on source-module
 * placement. Plugins receive the runtime in OnLoad.
 *
 * Declaration order is the dependency order: each service takes the siblings it uses by
 * reference, so a member may only be initialized from members declared above it. That order
 * also runs teardown in reverse, which is why a service's destructor may only touch what is
 * declared above it. Access specifiers do not affect either - only declaration order does.
 */
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * Start every subsystem and record its @ref LoadReport stage.
     * @return false when loading must abort; @p context.Error contains the reason.
     */
    bool Start(const LoadContext& context);

    /** Drive the scheduler. Called once per frame from the GameFrame hook. */
    void OnGameFrame();

    // Core services.
    /** Plugin-supplied permission, targeting and reply rules. Set once in OnLoad. */
    Core::PluginPolicy Policy;
    /** Named, timed load stages recorded by Start and by the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    Core::SlotEvents Slots;
    /** Frame pump, timers and delayed work. Pending timers are destroyed with it, never run, so
     *  whatever a timer's captured state owns may only point at members declared above this
     *  line. */
    Core::Scheduler Scheduler;
    /** Map the server is running, captured from StartupServer. Empty after a late load
     *  until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;

    Core::Translations Translations{Slots};

    // Engine-facing services.
    /** Plain interface-pointer holder; populated by Start. */
    Sdk::GameInterfaces Interfaces;
    Sdk::GameData GameData;

private:
    /** Internal schema-offset service, declared here because the engine services below take it.
     *  Constructed in Runtime::Runtime because the type is only forward-declared in this header.
     *  Depends on: Interfaces. */
    std::unique_ptr<Sdk::SchemaService> _schema;

public:
    /** Depends on: Interfaces, GameData, Schema(). */
    Sdk::EntitySystem Entities{Interfaces, GameData, *_schema};
    /** Pawn manipulations that need framework services, such as slap and its fall protection.
     *  Depends on: Scheduler, Slots, Entities. */
    Sdk::PawnService Pawns{Scheduler, Slots, Entities};
    /** Depends on: Entities, GameData, Schema(). */
    Sdk::EntityOpsService EntityOps{Entities, GameData, *_schema};
    /** Weapon give/list/strip through CCSPlayer_ItemServices. Depends on: Entities, GameData, Schema(). */
    Sdk::ItemService Items{Entities, GameData, *_schema};
    /** Depends on: Entities, GameData, Schema(), Slots. */
    Sdk::TransmitFilterService Transmit{Entities, GameData, *_schema, Slots};
    /** Builds per-viewer visibility effects (GlowVision). Depends on: Entities, EntityOps, Transmit. */
    Sdk::VisibilityService Visibility{Entities, EntityOps, Transmit};
    /** Depends on: GameData. */
    Sdk::PrecacheService Precache{GameData};
    /** Depends on: Interfaces. */
    Sdk::ConVarService ConVars{Interfaces};
    /** Map validation and level changes. Depends on: Interfaces, ConVars. */
    Sdk::MapService Maps{Interfaces, ConVars};
    /** Depends on: Interfaces, GameData. Declared before Messages, which sends through it. */
    Sdk::GameEventService Events{Interfaces, GameData};
    /** Depends on: Interfaces, GameData, Events, Translations. */
    Sdk::MessageSystem Messages{Interfaces, GameData, Events, Translations};
    /** The engine's simulation clock (tick and curtime). Depends on: Interfaces. */
    Sdk::ServerClock Clock{Interfaces};
    /** The game's own yes/no vote panel. Dormant until Start().
     *  Depends on: Interfaces, Entities, Schema(), Events, Scheduler. */
    Sdk::PanoramaVote Vote{Interfaces, Entities, *_schema, Events, Scheduler};
    /** Dormant until a plugin calls Install(); removes its vtable hook on destruction.
     *  Depends on: Entities, GameData. */
    Sdk::MovementHook MovementHook{Entities, GameData};
    /** Dormant until Install(); pre-damage listeners may rewrite or suppress the hit.
     *  Depends on: Entities, GameData. */
    Sdk::DamageHook Damage{Entities, GameData};
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars).
     *  Depends on: Interfaces. */
    Sdk::NetChannelService NetChannels{Interfaces};
    /** Depends on: Scheduler, Slots. */
    Sdk::ChatInputCapture ChatInput{Scheduler, Slots};
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes.
     *  Depends on: MovementHook, Slots. */
    Sdk::InputHistoryService InputHistory{MovementHook, Slots};
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn.
     *  Depends on: Entities, GameData, Events, Clock, Slots. */
    Sdk::TeleportTracker Teleports{Entities, GameData, Events, Clock, Slots};
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false).
     *  Depends on: Interfaces, GameData, Slots. */
    Sdk::ClientCvarService ClientCvars{Interfaces, GameData, Slots};

    // Composition-root services.
    /** Interfaces offered to, and borrowed from, other plugins. */
    App::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig.
     *  Depends on: Exchange, Scheduler. Withdraws in its dtor, while Exchange is still alive. */
    App::PluginIdentity Identity{Exchange, Scheduler};
    /** Status sections for diagnostics commands; framework sections registered by Start.
     *  Depends on: LoadReport. */
    App::StatusService Status{LoadReport};
    /** Registers its own per-frame input pump; both it and its slot listener stop in its dtor.
     *  Takes the whole runtime, so its constructor may only touch members declared above it -
     *  Scheduler and Slots, which is exactly what it subscribes to. */
    Menu::MenuManager Menus{*this};

    /** Internal schema-offset service (forward-declared type). */
    Sdk::SchemaService& Schema() { return *_schema; }

    // These names shadow their namespaces, so keep them last.
    Players::PlayerManager Players{Slots};
    /** Takes the whole runtime; its constructor only stores the reference, so every service it
     *  reads at dispatch time (Policy, Translations, Messages, Players) is live by then. */
    Commands::CommandManager Commands{*this};
    /** Completions dispatch on the game thread from a pump it registers itself; the dtor stops it. */
    Http::HttpClient Http{Scheduler};

private:
    // The four jobs Start does, in call order. A false return aborts the load and has already
    // written the reason into context.Error.
    void InstallLogger(const LoadContext& context);
    bool ResolveInterfaces(const LoadContext& context);
    bool InitializeServices(const LoadContext& context);
    void RegisterStatusSections();
};

}  // namespace VoltMod
