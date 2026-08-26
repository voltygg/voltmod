#pragma once

#include <VoltMod/App/PluginIdentity.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/Policy.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <VoltMod/Engine/NetChannel.hpp>
#include <VoltMod/Engine/Precache.hpp>
#include <VoltMod/Entities/Entity.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/Items.hpp>
#include <VoltMod/Entities/Pawns.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/ChatInput.hpp>
#include <VoltMod/Hooks/ClientCvars.hpp>
#include <VoltMod/Hooks/Damage.hpp>
#include <VoltMod/Hooks/InputHistory.hpp>
#include <VoltMod/Hooks/Movement.hpp>
#include <VoltMod/Hooks/Teleport.hpp>
#include <VoltMod/Hooks/Transmit.hpp>
#include <VoltMod/Hooks/Visibility.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Messaging/Vote.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <cstddef>
#include <memory>
#include <string>

namespace SourceMM
{
class ISmmAPI;
}
using SourceMM::ISmmAPI;

namespace VoltMod::Entities
{
class SchemaService;  // Internal type kept out of the public include graph.
}

namespace VoltMod
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    ISmmAPI* Ismm = nullptr;            ///< Metamod API pointer, from Plugin::Load
    char* Error = nullptr;              ///< Error buffer Metamod shows if the load fails
    size_t MaxLen = 0;                  ///< Size of that buffer
    bool Late = false;                  ///< Loaded after the server had already started
    const char* LogPrefix = "VoltMod";  ///< Console log prefix, e.g. "[ADMIN]"
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
    Core::Policy Policy;
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
    Engine::Interfaces Interfaces;
    Engine::GameData GameData;

private:
    /** Internal schema-offset service, declared here because the engine services below take it.
     *  Constructed in Runtime::Runtime because the type is only forward-declared in this header.
     *  Depends on: Interfaces. */
    std::unique_ptr<Entities::SchemaService> _schema;

public:
    /** Depends on: Interfaces, GameData, Schema(). */
    Entities::EntitySystem Entities{Interfaces, GameData, *_schema};
    /** Pawn manipulations that need framework services, such as slap and its fall protection.
     *  Depends on: Scheduler, Slots, Entities. */
    Entities::Pawns Pawns{Scheduler, Slots, Entities};
    /** Depends on: Entities, GameData, Schema(). */
    Entities::EntityOps EntityOps{Entities, GameData, *_schema};
    /** Weapon give/strip through CCSPlayer_ItemServices. Depends on: GameData, Schema(). */
    Entities::Items Items{GameData, *_schema};
    /** Depends on: Entities, GameData, Schema(), Slots. */
    Hooks::Transmit Transmit{Entities, GameData, *_schema, Slots};
    /** Builds per-viewer visibility effects (GlowVision). Depends on: Entities, EntityOps, Transmit. */
    Hooks::Visibility Visibility{Entities, EntityOps, Transmit};
    /** Depends on: GameData. */
    Engine::Precache Precache{GameData};
    /** Depends on: Interfaces. */
    Engine::ConVars ConVars{Interfaces};
    /** Map validation and level changes. Depends on: Interfaces, ConVars. */
    Engine::Map Maps{Interfaces, ConVars};
    /** Depends on: Interfaces, GameData. Declared before Messages, which sends through it. */
    Events::GameEvents Events{Interfaces, GameData};
    /** Depends on: Interfaces, GameData, Events, Translations. */
    Messaging::Messages Messages{Interfaces, GameData, Events, Translations};
    /** The engine's simulation clock (tick and curtime). Depends on: Interfaces. */
    Engine::Clock Clock{Interfaces};
    /** The game's own yes/no vote panel. Subscribes on the first StartVote().
     *  Depends on: Interfaces, Entities, Schema(), Events, Scheduler. */
    Messaging::Vote Vote{Interfaces, Entities, *_schema, Events, Scheduler};
    /** Dormant until a plugin calls Install(); removes its vtable hook on destruction.
     *  Depends on: Entities, GameData. */
    Hooks::Movement MovementHook{Entities, GameData};
    /** Dormant until Install(); observation only - listeners see each hit but cannot change it.
     *  Depends on: Entities, GameData. */
    Hooks::Damage Damage{Entities, GameData};
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars).
     *  Depends on: Interfaces. */
    Engine::NetChannels NetChannels{Interfaces};
    /** Depends on: Scheduler, Slots. */
    Hooks::ChatInput ChatInput{Scheduler, Slots};
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes.
     *  Depends on: MovementHook, Slots. */
    Hooks::InputHistory InputHistory{MovementHook, Slots};
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn.
     *  Depends on: Entities, GameData, Events, Clock, Slots. */
    Hooks::Teleport Teleports{Entities, GameData, Events, Clock, Slots};
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false).
     *  Depends on: Interfaces, GameData, Slots. */
    Hooks::ClientCvars ClientCvars{Interfaces, GameData, Slots};

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
    Entities::SchemaService& Schema() { return *_schema; }

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
