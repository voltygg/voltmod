#pragma once

#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/LoadSteps.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/ConVars/ConVars.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Server/Clock.hpp>
#include <VoltMod/Engine/Server/Map.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/World.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/Hooks.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/CenterHtmlMenu.hpp>
#include <VoltMod/Menu/MenuRouter.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <VoltMod/Unsafe/Unsafe.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/** What @ref Runtime::Start needs from the plugin module. */
struct LoadContext
{
    IHost* Host = nullptr;
    std::string_view Version;  ///< the plugin's build stamp
    char* Error = nullptr;     ///< shown by the host when the load fails
    size_t MaxLen = 0;
};

/**
 * @brief Framework services for one load and unload cycle.
 *
 * Members are declared in dependency order. Unsafe is initialized before services that use its
 * bindings. Screens and Addons follow the hook tiers so their hooks are removed first.
 */
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * Start every subsystem as a step in @ref LoadSteps.
     * @return false when loading must abort; @p context.Error contains the reason.
     */
    bool Start(const LoadContext& context);

    /** Drive the scheduler. Called once per frame from the GameFrame hook. */
    void OnGameFrame();

    /** The plugin's `plugin.json` name and directory under `addons/voltmod/plugins/`. */
    std::string PluginName;
    /** "<plugin.json version>+<short-sha>[-dirty]". */
    std::string Version;

    /** "addons/voltmod/plugins/<PluginName>/<relative>". */
    std::string PluginFile(std::string_view relative) const;

    VoltMod::LoadSteps LoadSteps;

    StatusService Status;

    SlotEvents Slots;

    /**
     * Per-frame delivery, timers, and delayed work. Pending timers are discarded on destruction;
     * their callbacks may capture only members declared above Scheduler.
     */
    VoltMod::Scheduler Scheduler;

    VoltMod::Translations Translations{Slots};

    /** The opt-in engine-access tier (Interfaces, Bindings). Populated by Start. */
    UnsafeServices Unsafe;

    /** Schema field offsets resolve themselves, per process rather than per load - see @ref Field. */
    EntitySystem Entities{Unsafe.Interfaces, Unsafe.Bindings};

    PlayerManager Players{Slots, &Entities};

    /** Plugin-supplied permission, targeting and reply rules, and the one gate that applies
     *  them (`Policy::Authorize`). Fill the members you enforce in Plugin::Load. */
    VoltMod::Policy Policy{Players};

    VoltMod::ConVars ConVars{Unsafe.Interfaces};

    /** Map validation and level changes. The current map is captured from StartupServer, so it
     *  is empty after a late load until the next map change. */
    VoltMod::Map Map{Unsafe.Interfaces, ConVars};

    VoltMod::GameEvents GameEvents{Unsafe.Interfaces, Unsafe.Bindings};

    VoltMod::Messages Messages{Unsafe.Interfaces, GameEvents, Translations};

    VoltMod::Clock Clock{Unsafe.Interfaces};

    WorldServices World{Entities, Unsafe.Bindings, Scheduler, Slots, Unsafe.Interfaces};

    HookServices Hooks{Entities, Unsafe.Bindings, Slots, Scheduler, GameEvents, Unsafe.Interfaces, World.EntityOps};

    VoltMod::ScreenManager Screens{Entities, World.EntityOps, Unsafe.Bindings, Unsafe.Interfaces,
                                   Slots,    Scheduler,       Hooks.Visibility};

    VoltMod::Addons Addons{Unsafe.Interfaces, Unsafe.Bindings, Players, Scheduler};

    ServiceExchange Exchange;

    /** Holds players still while a menu is open. Disabled by default. */
    VoltMod::MenuFreeze Freeze{Entities, Scheduler, Slots};

    CenterHtmlMenu CenterHtml{CenterHtmlMenu::Services{.Scheduler = Scheduler,
                                                       .Slots = Slots,
                                                       .Entities = Entities,
                                                       .Freeze = Freeze,
                                                       .ChatInput = Hooks.ChatInput,
                                                       .Translations = Translations,
                                                       .Policy = Policy,
                                                       .Messages = Messages}};

    /** Routes menus to center HTML or a plugin-preferred surface. */
    MenuRouter Menus{CenterHtml};

    VoltMod::CommandManager Commands{Policy, Translations, Players, Entities, Messages};

    /** HTTP client. Completions are replayed on the game thread. */
    HttpClient Http{Scheduler};

private:
    void InstallLogger(const LoadContext& context);
    bool ResolveInterfaces(const LoadContext& context);
    bool InitializeServices(const LoadContext& context);
    /** Take the host's one schema check, refusing the load unless it covers these baked offsets. */
    VoltMod::Status TakeHostSchema(const LoadContext& context);
    void RegisterStatusSections();
    /** Each optional feature that cannot work this load, with the reason. */
    std::map<std::string, std::string> UnavailableFeatures() const;
};

}  // namespace VoltMod
