#pragma once

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
#include <VoltMod/Menu/Html/HtmlMenuManager.hpp>
#include <VoltMod/Menu/Ui/UiMenuManager.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/UiPanel.hpp>
#include <VoltMod/Unsafe/Unsafe.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    SourceMM::ISmmAPI* Ismm = nullptr;       ///< Metamod API pointer, from Plugin::Load
    char* Error = nullptr;                   ///< Error buffer Metamod shows if the load fails
    size_t MaxLen = 0;                       ///< Size of that buffer
    std::string_view LogPrefix = "VoltMod";  ///< Console log prefix, e.g. "[ADMIN]"
};

/**
 * @brief Framework services for one Load/Unload cycle.
 *
 * Services are flat members named by role, with a few engine tiers grouped (@ref WorldServices,
 * @ref HookServices, @ref UnsafeServices): `runtime.Hooks.Movement`.
 *
 * Declaration order is dependency order. A member is initialized only from members above it,
 * and torn down before them, so a destructor may only touch what is declared above. Access
 * specifiers change none of that. @ref Unsafe comes early because nearly everything reads its
 * @ref Bindings; @ref Ui and @ref Addons sit below the hook tiers because each installs a vtable
 * hook that must come down before them.
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
    /** Named, timed load stages recorded by Start and by the plugin's OnLoad. */
    VoltMod::LoadReport LoadReport;
    /** What this load can do and why anything missing is missing. Written only by Start. */
    VoltMod::Capabilities Capabilities;
    /** Status sections for diagnostics commands; framework sections registered by Start. */
    StatusService Status{LoadReport};
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    SlotEvents Slots;
    /** Per-frame delivery, timers and delayed work. Pending timers die with it, never run, so
     *  what a timer captures may only point at members declared above. */
    VoltMod::Scheduler Scheduler;
    VoltMod::Translations Translations{Slots};

    /** The opt-in engine-access tier (Interfaces, GameData, Bindings). Populated by Start. */
    UnsafeServices Unsafe;

    /** Schema field offsets resolve themselves, per process rather than per load - see @ref Field. */
    EntitySystem Entities{Unsafe.Interfaces, Unsafe.Bindings};

    /** The roster and the connection lifecycle events. */
    PlayerManager Players{Slots, &Entities};
    /** Plugin-supplied permission, targeting and reply rules, and the one gate that applies
     *  them (`Policy::Authorize`). Fill the members you enforce in OnLoad. */
    VoltMod::Policy Policy{Players};

    VoltMod::ConVars ConVars{Unsafe.Interfaces};
    /** Map validation and level changes. The current map is captured from StartupServer, so it
     *  is empty after a late load until the next map change. */
    VoltMod::Map Map{Unsafe.Interfaces, ConVars};
    VoltMod::GameEvents GameEvents{Unsafe.Interfaces, Unsafe.Bindings};
    VoltMod::Messages Messages{Unsafe.Interfaces, Unsafe.Bindings, GameEvents, Translations};
    /** The engine's simulation clock (tick and curtime). */
    VoltMod::Clock Clock{Unsafe.Interfaces};

    /** Entity IO, weapon give/strip, precaching, pawn manipulation, per-client net reads. */
    WorldServices World{Entities, Unsafe.Bindings, Scheduler, Slots, Unsafe.Interfaces};
    /** The per-tick and per-event engine hooks. */
    HookServices Hooks{Entities, Unsafe.Bindings, Slots, Scheduler, GameEvents, Unsafe.Interfaces, World.EntityOps};
    /** Custom Panorama HUD layouts and the button presses coming back from them. */
    VoltMod::CustomUi Ui{Entities, World.EntityOps, Unsafe.Bindings, Unsafe.Interfaces, Slots, Scheduler};
    /** Workshop addons connecting clients are told to download. */
    VoltMod::Addons Addons{Unsafe.Interfaces, Unsafe.Bindings, Players, Scheduler};

    // Composition-root services.
    /** Interfaces offered to, and borrowed from, other plugins. */
    ServiceExchange Exchange;
    /** Center-HTML menus steered with WASD; works on every client. */
    HtmlMenuManager HtmlMenus{Scheduler, Slots, Entities, Messages, Hooks.ChatInput, Translations, Policy, Players};
    /** Clickable Panorama menus, for plugins that ship a layout. Spawns nothing until a menu opens. */
    UiMenuManager UiMenus{Scheduler, Ui, Slots, Entities, Hooks.ChatInput, Translations, Policy, Players};
    VoltMod::CommandManager Commands{Policy, Translations, Players, Entities, Messages};
    /** Completions replay on the game thread from a per-frame subscription it registers itself. */
    HttpClient Http{Scheduler};

private:
    // Start's jobs in call order; a false return has already written the reason into context.Error.
    void InstallLogger(const LoadContext& context);
    bool ResolveInterfaces(const LoadContext& context);
    bool InitializeServices(const LoadContext& context);
    void RegisterStatusSections();
};

}  // namespace VoltMod
