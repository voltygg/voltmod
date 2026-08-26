#pragma once

#include <VoltMod/App/PluginIdentity.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/Capabilities.hpp>
#include <VoltMod/Core/LoadReport.hpp>
#include <VoltMod/Core/Scheduler.hpp>
#include <VoltMod/Core/SlotEvents.hpp>
#include <VoltMod/Core/Translations.hpp>
#include <VoltMod/Engine/Bindings.hpp>
#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/GameData.hpp>
#include <VoltMod/Engine/Interfaces.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <VoltMod/Engine/NetChannel.hpp>
#include <VoltMod/Engine/Precache.hpp>
#include <VoltMod/Entities/EntityOps.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
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
#include <VoltMod/Players/Policy.hpp>
#include <cstddef>
#include <memory>
#include <string>

namespace VoltMod
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    SourceMM::ISmmAPI* Ismm = nullptr;  ///< Metamod API pointer, from Plugin::Load
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

    /** Checks the trailing canary and logs once via Log::Error if it no longer matches its
     *  initial value - a sign that something wrote past the end of this object. Called once
     *  per frame (OnGameFrame) and once more at destruction (~Runtime). */
    void VerifyIntegrity() const;

    // Core services.
    /** Named, timed load stages recorded by Start and by the plugin's OnLoad. */
    VoltMod::LoadReport LoadReport;
    /** What this load can actually do, and why anything missing is missing. Written only by
     *  Start; read it before using a feature whose gamedata or engine support can be absent. */
    VoltMod::Capabilities Capabilities;
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    SlotEvents Slots;
    /** Frame pump, timers and delayed work. Pending timers are destroyed with it, never run, so
     *  whatever a timer's captured state owns may only point at members declared above this
     *  line. */
    VoltMod::Scheduler Scheduler;
    /** Map the server is running, captured from StartupServer. Empty after a late load
     *  until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;

    VoltMod::Translations Translations{Slots};

    // Engine-facing services.
    /** Plain interface-pointer holder; populated by Start. */
    VoltMod::Interfaces Interfaces;
    /** The raw gamedata resolutions, for diagnostics. Services read @ref Bindings instead. */
    VoltMod::GameData GameData;
    /** The typed view of GameData; bound once by Start and handed to every engine service. */
    VoltMod::Bindings Bindings;

    /** Depends on: Interfaces, Bindings. Schema field offsets resolve themselves, per process
     *  rather than per load - see @ref Field. */
    EntitySystem Entities{Interfaces, Bindings};

    /** The roster and the connection lifecycle events. Declared here, above everything that
     *  resolves a player, because Policy holds it. Depends on: Slots, Entities. */
    PlayerManager Players{Slots, &Entities};
    /** Plugin-supplied permission, targeting and reply rules, and the one gate that applies
     *  them (`Policy::Authorize`). Fill the members you enforce in OnLoad. Depends on: Players. */
    VoltMod::Policy Policy{Players};

    /** Pawn manipulations that need framework services, such as slap and its fall protection.
     *  Depends on: Scheduler, Slots, Entities. */
    VoltMod::Pawns Pawns{Scheduler, Slots, Entities};
    /** Depends on: Entities, Bindings. */
    VoltMod::EntityOps EntityOps{Entities, Bindings};
    /** Weapon give/strip through CCSPlayer_ItemServices. Depends on: Bindings. */
    VoltMod::Items Items{Bindings};
    /** Depends on: Entities, Bindings, Slots. */
    VoltMod::Transmit Transmit{Entities, Bindings, Slots};
    /** Builds per-viewer visibility effects (GlowVision). Depends on: Entities, EntityOps, Transmit. */
    VoltMod::Visibility Visibility{Entities, EntityOps, Transmit};
    /** Depends on: Bindings. */
    VoltMod::Precache Precache{Bindings};
    /** Depends on: Interfaces. */
    VoltMod::ConVars ConVars{Interfaces};
    /** Map validation and level changes. Depends on: Interfaces, ConVars. */
    Map Maps{Interfaces, ConVars};
    /** Depends on: Interfaces, Bindings. Declared before Messages, which sends through it. */
    GameEvents Events{Interfaces, Bindings};
    /** Depends on: Interfaces, Bindings, Events, Translations. */
    VoltMod::Messages Messages{Interfaces, Bindings, Events, Translations};
    /** The engine's simulation clock (tick and curtime). Depends on: Interfaces. */
    VoltMod::Clock Clock{Interfaces};
    /** The game's own yes/no vote panel. Subscribes on the first StartVote().
     *  Depends on: Interfaces, Entities, Events, Scheduler. */
    VoltMod::Vote Vote{Interfaces, Entities, Events, Scheduler};
    /** Dormant until something subscribes; the last subscription dropped removes the vtable hook.
     *  Depends on: Entities, Bindings. */
    Movement MovementHook{Entities, Bindings};
    /** Dormant until something subscribes to Hit; observation only - listeners see each hit but
     *  cannot change it. Depends on: Entities, Bindings. */
    VoltMod::Damage Damage{Entities, Bindings};
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars).
     *  Depends on: Interfaces. */
    VoltMod::NetChannels NetChannels{Interfaces};
    /** Depends on: Scheduler, Slots. */
    VoltMod::ChatInput ChatInput{Scheduler, Slots};
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes.
     *  Depends on: MovementHook, Slots. */
    VoltMod::InputHistory InputHistory{MovementHook, Slots};
    /** Dormant until something subscribes to Teleports.Teleported; per-pawn Teleport hook re-bound
     *  on PlayerSpawn. Depends on: Entities, Bindings, Events, Slots. */
    Teleport Teleports{Entities, Bindings, Events, Slots};
    /** Async client-side convar reads. Inert when Capability::ClientCvars is off.
     *  Depends on: Interfaces, Bindings, Slots. */
    VoltMod::ClientCvars ClientCvars{Interfaces, Bindings, Slots};

    // Composition-root services.
    /** Interfaces offered to, and borrowed from, other plugins. */
    ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig.
     *  Depends on: Exchange, Scheduler. Withdraws in its dtor, while Exchange is still alive. */
    PluginIdentity Identity{Exchange, Scheduler};
    /** Status sections for diagnostics commands; framework sections registered by Start.
     *  Depends on: LoadReport. */
    StatusService Status{LoadReport};
    /** Registers its own per-frame input pump; both it and its slot listener stop in its dtor.
     *  Takes exactly the services it and its context rows use - all declared above it. */
    MenuManager Menus{Scheduler, Slots, Entities, Messages, ChatInput, Translations, Policy, Players};

    /** Depends on: Policy, Translations, Players, Entities, Messages - the five services
     *  command dispatch reaches, taken directly rather than through the runtime. */
    VoltMod::CommandManager Commands{Policy, Translations, Players, Entities, Messages};
    /** Completions dispatch on the game thread from a pump it registers itself; the dtor stops it. */
    HttpClient Http{Scheduler};

private:
    // The four jobs Start does, in call order. A false return aborts the load and has already
    // written the reason into context.Error.
    void InstallLogger(const LoadContext& context);
    bool ResolveInterfaces(const LoadContext& context);
    bool InitializeServices(const LoadContext& context);
    void RegisterStatusSections();

    /** Set once VerifyIntegrity() has logged a corrupted canary, so it does not spam every frame. */
    mutable bool _canaryReported = false;
    /** Value _tail is expected to hold; compared against directly so a corrupted _tail cannot
     *  hide the corruption by also changing what it is checked against. */
    static constexpr uint64_t kCanaryValue = 0x564F4C544D4F4400ull;
    /** Canary; must stay the last data member. An out-of-bounds write into this Runtime is far
     *  more likely to land here than to corrupt a member declared earlier, turning a silent
     *  latent bug (see the +8-byte sizeof(Runtime) crash this guards against) into a logged one.
     *  Checked by VerifyIntegrity(). */
    uint64_t _tail = kCanaryValue;
};

}  // namespace VoltMod
