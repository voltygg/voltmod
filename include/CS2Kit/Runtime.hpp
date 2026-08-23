#pragma once

#include <CS2Kit/App/PluginIdentity.hpp>
#include <CS2Kit/App/ServiceExchange.hpp>
#include <CS2Kit/App/StatusService.hpp>
#include <CS2Kit/Commands/CommandManager.hpp>
#include <CS2Kit/Core/ILogger.hpp>
#include <CS2Kit/Core/LoadReport.hpp>
#include <CS2Kit/Core/PluginPolicy.hpp>
#include <CS2Kit/Core/Scheduler.hpp>
#include <CS2Kit/Core/SlotEvents.hpp>
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
#include <cstddef>
#include <memory>
#include <string>

namespace SourceMM
{
class ISmmAPI;
}
using SourceMM::ISmmAPI;

namespace CS2Kit::Sdk
{
class SchemaService;  // internal (src/Sdk/Schema.hpp) - by pointer, so this header stays clean
}

namespace CS2Kit
{

/** Everything @ref Runtime::Start needs from Metamod, plus the optional overrides. */
struct LoadContext
{
    ISmmAPI* Ismm = nullptr;             ///< Metamod API pointer, from Plugin::Load
    char* Error = nullptr;               ///< Error buffer Metamod shows if the load fails
    size_t MaxLen = 0;                   ///< Size of that buffer
    bool Late = false;                   ///< Loaded after the server had already started
    const char* LogPrefix = "CS2Kit";    ///< Console log prefix, e.g. "[ADMIN]"
    const char* GameDataPath = nullptr;  ///< Override signatures.jsonc; null uses the shipped one
    Core::ILogger* Logger = nullptr;     ///< Custom logger; null uses the console logger
};

/**
 * @brief Every kit service, owned as one unit for one Load/Unload cycle.
 *
 * One flat container, not a tree of per-layer ones: the layering that matters is between
 * source modules, and mirroring it in the runtime only meant plugins spelled out which
 * module owned a service (`Sdk.Messages`) instead of naming the service (`Messages`).
 * Services move between modules; the names here should not move with them.
 *
 * Declaration order is construction order and reverse destruction order, so the three
 * inter-member dependencies below (everything taking @ref Slots) are satisfied by position.
 * Plugins receive this by reference in OnLoad; kit modules reach it via @ref Detail::Rt.
 */
class Runtime
{
public:
    Runtime();
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /**
     * Resolve the SDK interfaces, load gamedata and bring up every subsystem, recording each
     * as a @ref LoadReport stage. False means the load must abort; @p context.Error carries why.
     */
    bool Start(const LoadContext& context);

    /** Drive the scheduler. Called once per frame from the GameFrame hook. */
    void OnGameFrame();

    /** Drop per-slot state across the services that hold any. */
    void OnPlayerDisconnect(int slot);

    // --- Core: primitives every service may reach ---
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

    /** Translated player-facing text. */
    Utils::Translations Translations;

    // --- Sdk: everything engine-facing ---
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

    // --- App: composition-root services ---
    /** Interfaces offered to, and borrowed from, other plugins. */
    App::ServiceExchange Exchange;
    /** This plugin's manifest, published to peers. Filled by LoadStandardConfig. */
    App::PluginIdentity Identity;
    /** Status sections for diagnostics commands; kit sections registered by Start. */
    App::StatusService Status;
    Menu::MenuManager Menus;

    /** Internal schema-offset service (forward-declared type). */
    Sdk::SchemaService& Schema() { return *_schema; }

    // Declared last: each name shadows its own namespace for the rest of the class body.
    Players::PlayerManager Players{Slots};
    Commands::CommandManager Commands;
    /** Completions dispatch on the game thread from the scheduler pump; the dtor stops it. */
    Http::HttpClient Http;

private:
    std::unique_ptr<Sdk::SchemaService> _schema;
};

}  // namespace CS2Kit
