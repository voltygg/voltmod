#pragma once

#include <VoltMod/App/Internal/HostPlayerLanguages.hpp>
#include <VoltMod/App/ServiceExchange.hpp>
#include <VoltMod/App/StatusService.hpp>
#include <VoltMod/Commands/CommandManager.hpp>
#include <VoltMod/Core/LoadSteps.hpp>
#include <VoltMod/Core/Signals/Subscription.hpp>
#include <VoltMod/Core/Slots/SlotEvents.hpp>
#include <VoltMod/Core/Text/Translations.hpp>
#include <VoltMod/Core/Time/Scheduler.hpp>
#include <VoltMod/Engine/ConVars/ConVar.hpp>
#include <VoltMod/Engine/EngineTypes.hpp>
#include <VoltMod/Engine/Net/NetChannel.hpp>
#include <VoltMod/Engine/Server/Clock.hpp>
#include <VoltMod/Engine/Server/Map.hpp>
#include <VoltMod/Engine/Server/Precache.hpp>
#include <VoltMod/Entities/EntitySystem.hpp>
#include <VoltMod/Entities/Rounds.hpp>
#include <VoltMod/Entities/Trace.hpp>
#include <VoltMod/Events/GameEvents.hpp>
#include <VoltMod/Hooks/HookServices.hpp>
#include <VoltMod/Host/IHost.hpp>
#include <VoltMod/Http/HttpClient.hpp>
#include <VoltMod/Menu/CenterHtmlMenu.hpp>
#include <VoltMod/Menu/MenuRouter.hpp>
#include <VoltMod/Menu/PanoramaMenuLayout.hpp>
#include <VoltMod/Messaging/Messages.hpp>
#include <VoltMod/Players/PlayerManager.hpp>
#include <VoltMod/Players/Policy.hpp>
#include <VoltMod/Ui/ScreenManager.hpp>
#include <VoltMod/Unsafe/UnsafeServices.hpp>
#include <VoltMod/Workshop/Addons.hpp>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace VoltMod
{

/**
 * @brief Framework services for one load and unload cycle.
 *
 * Members are declared in dependency order, and each engine service does its setup when built;
 * a feature that did not bind says why in its `Available()` and in @ref LoadSteps. Screens and
 * Addons follow the hook tiers so their hooks are removed first.
 */
class Runtime
{
public:
    /** @p host and @p unsafe outlive the runtime; the plugin module opened @p unsafe first. */
    Runtime(IHost& host, UnsafeServices& unsafe);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /** Drive the scheduler. Called once per frame from the GameFrame hook. */
    void OnGameFrame();

    /** The plugin's `plugin.json` name and directory under `addons/voltmod/plugins/`. */
    const std::string PluginName;
    /** The plugin's `plugin.json` version. */
    const std::string Version;

    /** "addons/voltmod/plugins/<PluginName>/<relative>". */
    std::string PluginFile(std::string_view relative) const;

    /**
     * Draw menus on @p layout for players who have it, center HTML for the rest, while the
     * returned Subscription lives. Hold it below @p layout, which the menu refers to.
     * @param addonId the workshop addon clients need for the layout; zero when it is built in.
     */
    [[nodiscard]] Subscription UsePanorama(PanoramaMenuLayout& layout, uint64_t addonId);

    VoltMod::LoadSteps LoadSteps;

    StatusService Status;

    SlotEvents Slots;

    /**
     * Per-frame delivery, timers, and delayed work. Pending timers are discarded on destruction;
     * their callbacks may capture only members declared above Scheduler.
     */
    VoltMod::Scheduler Scheduler;

private:
    Internal::HostPlayerLanguages _languages;

public:
    VoltMod::Translations Translations{_languages};

    /** The opt-in engine-access tier (Interfaces, Bindings), resolved before any service. */
    UnsafeServices& Unsafe;

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

    /** Resources for the next map's session manifest. */
    VoltMod::Precache Precache;

    /** Per-client latency and replicated userinfo cvars. */
    VoltMod::NetChannels NetChannels{Unsafe.Interfaces};

    /** Line and box traces for sight and reachability questions. */
    VoltMod::Trace Trace{Unsafe.Bindings};

    /** Ending the current round with a winner. */
    VoltMod::Rounds Rounds{Entities, Unsafe.Bindings};

    HookServices Hooks{Entities, Unsafe.Bindings, Slots, Scheduler, GameEvents, Unsafe.Interfaces};

    VoltMod::ScreenManager Screens{Entities, Unsafe.Bindings, Unsafe.Interfaces, Slots, Scheduler, Hooks.Visibility};

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

    VoltMod::CommandManager Commands;

    /** HTTP client. Completions are replayed on the game thread. */
    HttpClient Http{Scheduler};

private:
    /** Record each service as a load step: Messages required, the rest optional. */
    void RecordServiceSteps();
    void RegisterStatusSections();
    /** Each optional feature that cannot work this load, with the reason. */
    std::map<std::string, std::string> UnavailableFeatures() const;
};

}  // namespace VoltMod
