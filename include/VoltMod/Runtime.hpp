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
#include <VoltMod/Engine/Clock.hpp>
#include <VoltMod/Engine/ConVars.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Map.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/World.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Hooks.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/MenuManager.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Unsafe/Unsafe.hpp>
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
 * Services are flat, by role, so their public names do not depend on source-module placement:
 * most are direct members, and a few niche engine tiers are grouped into a struct that owns
 * them - @ref WorldServices, @ref HookServices and @ref UnsafeServices - so a plugin author
 * skimming this class sees the shape of what it offers instead of thirty peers in one list.
 * Reach a grouped service through its group, e.g. `runtime.Hooks.Movement`.
 *
 * Declaration order is the dependency order: each service (or group) takes the siblings it uses
 * by reference, so a member may only be initialized from members declared above it. That order
 * also runs teardown in reverse, which is why a service's destructor may only touch what is
 * declared above it. Access specifiers do not affect either - only declaration order does. A
 * group states this once for the services it owns, in its own header, rather than once per
 * member here.
 *
 * @ref UnsafeServices is declared right after the services with no dependencies at all, ahead of
 * almost everything else: nearly every other service reads its @ref Bindings (or resolves
 * through its @ref Interfaces), the opposite of how late "opt-in tier" reads in the list below.
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
    /** Status sections for diagnostics commands; framework sections registered by Start.
     *  Depends on: LoadReport. */
    StatusService Status{LoadReport};
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    SlotEvents Slots;
    /** Frame pump, timers and delayed work. Pending timers are destroyed with it, never run, so
     *  whatever a timer's captured state owns may only point at members declared above this
     *  line. */
    VoltMod::Scheduler Scheduler;
    VoltMod::Translations Translations{Slots};

    /** The opt-in engine-access tier (Interfaces, GameData, Bindings) - see @ref UnsafeServices
     *  for why it is declared this early. Populated by Start. */
    UnsafeServices Unsafe;

    /** Depends on: Unsafe.Interfaces, Unsafe.Bindings. Schema field offsets resolve themselves,
     *  per process rather than per load - see @ref Field. */
    EntitySystem Entities{Unsafe.Interfaces, Unsafe.Bindings};

    /** The roster and the connection lifecycle events. Declared here, above everything that
     *  resolves a player, because Policy holds it. Depends on: Slots, Entities. */
    PlayerManager Players{Slots, &Entities};
    /** Plugin-supplied permission, targeting and reply rules, and the one gate that applies
     *  them (`Policy::Authorize`). Fill the members you enforce in OnLoad. Depends on: Players. */
    VoltMod::Policy Policy{Players};

    /** Depends on: Unsafe.Interfaces. */
    VoltMod::ConVars ConVars{Unsafe.Interfaces};
    /** Map validation and level changes, and the map the server is running (captured from
     *  StartupServer; empty after a late load until the next map change, since the hook has
     *  already fired by then). Depends on: Unsafe.Interfaces, ConVars. */
    VoltMod::Map Map{Unsafe.Interfaces, ConVars};
    /** Depends on: Unsafe.Interfaces, Unsafe.Bindings. Declared before Messages, which sends
     *  through it. */
    VoltMod::GameEvents GameEvents{Unsafe.Interfaces, Unsafe.Bindings};
    /** Depends on: Unsafe.Interfaces, Unsafe.Bindings, GameEvents, Translations. */
    VoltMod::Messages Messages{Unsafe.Interfaces, Unsafe.Bindings, GameEvents, Translations};
    /** The engine's simulation clock (tick and curtime). Depends on: Unsafe.Interfaces. */
    VoltMod::Clock Clock{Unsafe.Interfaces};

    /** Entity IO, weapon give/strip, precaching, pawn manipulation, and per-client net-channel
     *  reads - see @ref WorldServices. Depends on: Entities, Unsafe.Bindings, Scheduler, Slots,
     *  Unsafe.Interfaces. */
    WorldServices World{Entities, Unsafe.Bindings, Scheduler, Slots, Unsafe.Interfaces};
    /** The per-tick and per-event engine hooks - see @ref HookServices. Depends on: Entities,
     *  Unsafe.Bindings, Slots, Scheduler, GameEvents, Unsafe.Interfaces, World.EntityOps. */
    HookServices Hooks{Entities, Unsafe.Bindings, Slots, Scheduler, GameEvents, Unsafe.Interfaces, World.EntityOps};

    // Composition-root services.
    /** Interfaces offered to, and borrowed from, other plugins. */
    ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig.
     *  Depends on: Exchange, Scheduler. Withdraws in its dtor, while Exchange is still alive. */
    PluginIdentity Identity{Exchange, Scheduler};
    /** Registers its own per-frame input pump; both it and its slot listener stop in its dtor.
     *  Takes exactly the services it and its context rows use - all declared above it. Depends
     *  on: Scheduler, Slots, Entities, Messages, Hooks.ChatInput, Translations, Policy, Players.*/
    MenuManager Menus{Scheduler, Slots, Entities, Messages, Hooks.ChatInput, Translations, Policy, Players};

    /** Depends on: Policy, Translations, Players, Entities, Messages - the five services
     *  command dispatch reaches, taken directly rather than through the runtime. */
    VoltMod::CommandManager Commands{Policy, Translations, Players, Entities, Messages};
    /** Completions dispatch on the game thread from a pump it registers itself; the dtor stops
     *  it. Depends on: Scheduler. */
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
