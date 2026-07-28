#pragma once

#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/PluginPolicy.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/StatusService.hpp>
#include <CS2Kit/Http/HttpClient.hpp>
#include <CS2Kit/Menu/MenuManager.hpp>
#include <CS2Kit/Players/PlayerManager.hpp>
#include <CS2Kit/Sdk/ChatInputCapture.hpp>
#include <CS2Kit/Sdk/ClientCvarService.hpp>
#include <CS2Kit/Sdk/ConVarService.hpp>
#include <CS2Kit/Sdk/Entity.hpp>
#include <CS2Kit/Sdk/EntityOps.hpp>
#include <CS2Kit/Sdk/GameData.hpp>
#include <CS2Kit/Sdk/GameEventService.hpp>
#include <CS2Kit/Sdk/GameInterfaces.hpp>
#include <CS2Kit/Sdk/InputHistoryService.hpp>
#include <CS2Kit/Sdk/MovementHook.hpp>
#include <CS2Kit/Sdk/NetChannel.hpp>
#include <CS2Kit/Sdk/PrecacheService.hpp>
#include <CS2Kit/Sdk/TeleportTracker.hpp>
#include <CS2Kit/Sdk/TransmitFilter.hpp>
#include <CS2Kit/Sdk/UserMessage.hpp>
#include <CS2Kit/Utils/Translations.hpp>
#include <memory>
#include <string>

namespace CS2Kit::Sdk
{
class SchemaService;  // internal (src/Sdk/Schema.hpp) - held by pointer so this public header stays clean
}

namespace CS2Kit::Core
{

/**
 * @brief Owns every CS2Kit service for one Load/Unload cycle.
 *
 * Replaces the per-class process-lifetime singletons: the plugin constructs one
 * Services on Load and destroys it on Unload, so service state cannot leak across
 * `meta unload`/`meta reload`. Members are declared in dependency order;
 * destruction is the reverse (RAII).
 *
 * Reach a service via @ref Engine() (e.g. `Engine().Players`, `Engine().Schema()`).
 */
class Services
{
public:
    Services();
    ~Services();
    Services(const Services&) = delete;
    Services& operator=(const Services&) = delete;

    // Declaration order == construction order.
    /** Plugin-supplied policy (permissions, targeting, replies). Set once in OnLoad. */
    PluginPolicy Policy;
    /** Named/timed load stages recorded by Initialize and the plugin's OnLoad. */
    Core::LoadReport LoadReport;
    /** Map the server is running, captured from the StartupServer hook. Empty after a late
     *  (mid-map) load until the next map change, since the hook has already fired by then. */
    std::string CurrentMap;
    /** Status sections for diagnostics commands; kit sections registered during load. */
    Core::StatusService Status;
    Sdk::GameInterfaces Interfaces;  // plain interface-pointer holder; populated in CS2Kit::Initialize
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
    Core::Scheduler Scheduler;
    Sdk::ChatInputCapture ChatInput;
    Utils::Translations Translations;
    Players::PlayerManager Players;
    /** Dormant until Enable(depth); listens on MovementHook cmd feed + Players slot changes. */
    Sdk::InputHistoryService InputHistory;
    /** Dormant until Enable(); per-pawn Teleport hook re-bound on PlayerSpawn. */
    Sdk::TeleportTracker Teleports;
    /** Async client-side convar reads. Inert when its load stage degraded (Available() == false). */
    Sdk::ClientCvarService ClientCvars;
    Commands::CommandManager Commands;
    Menu::MenuManager Menus;
    /** Completions dispatch on the game thread from the OnGameFrame pump; Shutdown stops it. */
    Http::HttpClient Http;

    /** Internal schema-offset service (forward-declared type). */
    Sdk::SchemaService& Schema() { return *_schema; }

private:
    std::unique_ptr<Sdk::SchemaService> _schema;  // constructed last, before public members are used
};

/** Set/clear the active Services backing @ref Engine(). Called by MetamodPluginBase on Load/Unload. */
void SetActiveServices(Services* services);

/** The active Services. Asserts if called outside a Load/Unload window. */
Services& Engine();

/** The active Services, or nullptr - for teardown paths that may run after Shutdown. */
Services* EngineOrNull();

}  // namespace CS2Kit::Core
