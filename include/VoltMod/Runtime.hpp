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
#include <VoltMod/Sdk/Engine/NetChannel.hpp>
#include <VoltMod/Sdk/Engine/PrecacheService.hpp>
#include <VoltMod/Sdk/Entity/Entity.hpp>
#include <VoltMod/Sdk/Entity/EntityOps.hpp>
#include <VoltMod/Sdk/Events/GameEventService.hpp>
#include <VoltMod/Sdk/Messaging/ChatInputCapture.hpp>
#include <VoltMod/Sdk/Messaging/UserMessage.hpp>
#include <VoltMod/Sdk/Movement/InputHistoryService.hpp>
#include <VoltMod/Sdk/Movement/MovementHook.hpp>
#include <VoltMod/Sdk/Movement/TeleportTracker.hpp>
#include <VoltMod/Sdk/Visibility/TransmitFilter.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

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
 * placement. Declaration order satisfies member dependencies and controls
 * reverse-order teardown. Plugins receive the runtime in OnLoad.
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

    /** Drop per-slot state across the services that hold any. */
    void OnPlayerDisconnect(int slot);

    // Core services.
    /** Plugin-supplied permission, targeting and reply rules. Set once in OnLoad. */
    Core::PluginPolicy Policy;
    /** Named, timed load stages recorded by Start and by the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** Frame pump, timers and delayed work. */
    Core::Scheduler Scheduler;
    /** "This slot changed hands", raised by the roster and consumed by per-slot caches. */
    Core::SlotEvents Slots;
    /** Map the server is running, captured from StartupServer. Empty after a late load
     *  until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;

    Core::Translations Translations;

    // Engine-facing services.
    /** Plain interface-pointer holder; populated by Start. */
    Sdk::GameInterfaces Interfaces;
    Sdk::GameData GameData;
    Sdk::MessageSystem Messages;
    Sdk::EntitySystem Entities;
    Sdk::EntityOpsService EntityOps;
    Sdk::TransmitFilterService Transmit;
    Sdk::PrecacheService Precache;
    Sdk::ConVarService ConVars;
    /** Dormant until a plugin calls Install(); removes its vtable hook on destruction. */
    Sdk::MovementHook MovementHook;
    /** Stateless per-client net-channel reads (latency, replicated userinfo cvars). */
    Sdk::NetChannelService NetChannels;
    Sdk::GameEventService Events;
    Sdk::ChatInputCapture ChatInput;
    /** Dormant until Enable(depth); listens on the MovementHook cmd feed + slot changes. */
    Sdk::InputHistoryService InputHistory{Slots};
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn. */
    Sdk::TeleportTracker Teleports{Slots};
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false). */
    Sdk::ClientCvarService ClientCvars;

    // Composition-root services.
    /** Interfaces offered to, and borrowed from, other plugins. */
    App::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig. */
    App::PluginIdentity Identity;
    /** Status sections for diagnostics commands; framework sections registered by Start. */
    App::StatusService Status;
    Menu::MenuManager Menus;

    /** Internal schema-offset service (forward-declared type). */
    Sdk::SchemaService& Schema() { return *_schema; }

    // These names shadow their namespaces, so keep them last.
    Players::PlayerManager Players{Slots};
    Commands::CommandManager Commands;
    /** Completions dispatch on the game thread from the scheduler pump; the dtor stops it. */
    Http::HttpClient Http;

private:
    std::unique_ptr<Sdk::SchemaService> _schema;
    /** Declared after Scheduler so these unregister while it is still alive. */
    std::vector<Core::Subscription> _frameTimers;
};

}  // namespace VoltMod
